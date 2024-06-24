/* Maxmemory directive handling (LRU eviction and other policies).
 *
 * ----------------------------------------------------------------------------
 *
 * Copyright (c) 2009-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "server.h"
#include "bio.h"
#include "atomicvar.h"

/* ----------------------------------------------------------------------------
 * Data structures
 * --------------------------------------------------------------------------*/

/* To improve the quality of the LRU approximation we take a set of keys
 * that are good candidate for eviction across freeMemoryIfNeeded() calls.
 *
 * Entries inside the eviciton pool are taken ordered by idle time, putting
 * greater idle times to the right (ascending order).
 *
 * When an LFU policy is used instead, a reverse frequency indication is used
 * instead of the idle time, so that we still evict by larger value (larger
 * inverse frequency means to evict keys with the least frequent accesses).
 *
 * Empty entries have the key pointer set to NULL. */
#define EVPOOL_SIZE 16
#define EVPOOL_CACHED_SDS_SIZE 255
struct evictionPoolEntry {
    unsigned long long idle;    /* 待淘汰的键值对的空闲时间 */
    sds key;                    /* 待淘汰的键值对的key */
    sds cached;                 /* Cached SDS object for key name. */
    int dbid;                   /* 待淘汰的键值对的key所在的数据库id */
};

static struct evictionPoolEntry *EvictionPoolLRU;

/* ----------------------------------------------------------------------------
 * Implementation of eviction, aging and LRU
 * --------------------------------------------------------------------------*/

/* Return the LRU clock, based on the clock resolution. This is a time
 * in a reduced-bits format that can be used to set and check the
 * object->lru field of redisObject structures.
 * 获取全局LRU时钟,精度为1s
 * */
unsigned int getLRUClock(void) {
    return (mstime() / LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}

/* This function is used to obtain the current LRU clock.
 * If the current resolution is lower than the frequency we refresh the
 * LRU clock (as it should be in production servers) we return the
 * precomputed value, otherwise we need to resort to a system call. */
unsigned int LRU_CLOCK(void) {
    unsigned int lruclock;
    if (1000 / server.hz <= LRU_CLOCK_RESOLUTION) {
        atomicGet(server.lruclock, lruclock);
    } else {
        lruclock = getLRUClock();
    }
    return lruclock;
}

/* Given an object returns the min number of milliseconds the object was never
 * requested, using an approximated LRU algorithm. */
unsigned long long estimateObjectIdleTime(robj *o) {
    unsigned long long lruclock = LRU_CLOCK();
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * LRU_CLOCK_RESOLUTION;
    } else {
        return (lruclock + (LRU_CLOCK_MAX - o->lru)) *
               LRU_CLOCK_RESOLUTION;
    }
}

/* freeMemoryIfNeeded() gets called when 'maxmemory' is set on the config
 * file to limit the max memory used by the server, before processing a
 * command.
 *
 * The goal of the function is to free enough memory to keep Redis under the
 * configured memory limit.
 *
 * The function starts calculating how many bytes should be freed to keep
 * Redis under the limit, and enters a loop selecting the best keys to
 * evict accordingly to the configured policy.
 *
 * If all the bytes needed to return back under the limit were freed the
 * function returns C_OK, otherwise C_ERR is returned, and the caller
 * should block the execution of commands that will result in more memory
 * used by the server.
 *
 * ------------------------------------------------------------------------
 *
 * LRU approximation algorithm
 *
 * Redis uses an approximation of the LRU algorithm that runs in constant
 * memory. Every time there is a key to expire, we sample N keys (with
 * N very small, usually in around 5) to populate a pool of best keys to
 * evict of M keys (the pool size is defined by EVPOOL_SIZE).
 *
 * The N keys sampled are added in the pool of good keys to expire (the one
 * with an old access time) if they are better than one of the current keys
 * in the pool.
 *
 * After the pool is populated, the best key we have in the pool is expired.
 * However note that we don't remove keys from the pool when they are deleted
 * so the pool may contain keys that no longer exist.
 *
 * When we try to evict a key, and all the entries in the pool don't exist
 * we populate it again. This time we'll be sure that the pool has at least
 * one key that can be evicted, if there is at least one key that can be
 * evicted in the whole database. */

/* Create a new eviction pool. */
void evictionPoolAlloc(void) {
    struct evictionPoolEntry *ep;
    int j;

    ep = zmalloc(sizeof(*ep) * EVPOOL_SIZE);
    for (j = 0; j < EVPOOL_SIZE; j++) {
        ep[j].idle = 0;
        ep[j].key = NULL;
        ep[j].cached = sdsnewlen(NULL, EVPOOL_CACHED_SDS_SIZE);
        ep[j].dbid = 0;
    }
    EvictionPoolLRU = ep;
}

/* 从给定的数据集中采集样本填充到样本池中.
 * 样本池中的数据按淘汰优先级排序,越优先淘汰的数据越在后面.
 * sampledict根据maxmemory-policy配置来选择的,如果是allkeys-lru,那么就是redis server的全局hash表,
 * 否则就是保存着设置了过期时间的key的hash表
 * */
void evictionPoolPopulate(int dbid, dict *sampledict, dict *keydict, struct evictionPoolEntry *pool) {
    int j, k, count;
    dictEntry *samples[server.maxmemory_samples];
    count = dictGetSomeKeys(sampledict, samples, server.maxmemory_samples);
    for (j = 0; j < count; j++) {
        unsigned long long idle;
        sds key;
        robj *o;
        dictEntry *de;
        // 获取样本数据对应的键值对
        de = samples[j];
        key = dictGetKey(de);

        /* 如果我们采样的字典不是主字典（而是过期字典），我们需要在键字典中再次查找键来获取值对象。 */
        if (server.maxmemory_policy != MAXMEMORY_VOLATILE_TTL) {
            if (sampledict != keydict) de = dictFind(keydict, key);
            o = dictGetVal(de);
        }

        /* 计算淘汰优先级idle,idle越大,淘汰优先级越高 */
        if (server.maxmemory_policy & MAXMEMORY_FLAG_LRU) {
            //计算采样集合中的每一个键值对的空闲时间
            idle = estimateObjectIdleTime(o);
        } else if (server.maxmemory_policy & MAXMEMORY_FLAG_LFU) {
            /* 键值对访问次数越大, idle越小 */
            idle = 255 - LFUDecrAndReturn(o);
        } else if (server.maxmemory_policy == MAXMEMORY_VOLATILE_TTL) {
            /* In this case the sooner the expire the better. */
            idle = ULLONG_MAX - (long) dictGetVal(de);
        } else {
            serverPanic("Unknown eviction policy in evictionPoolPopulate()");
        }

        /* 尝试把采样的每一个键值对插入 EvictionPoolLRU 数组，这主要取决于以下两个条件之一：
         * 一是，它能在数组中找到一个尚未插入键值对的空位；
         * 二是，它能在数组中找到一个空闲时间小于采样键值对空闲时间的键值对。
         * */
        k = 0;
        while (k < EVPOOL_SIZE && pool[k].key && pool[k].idle < idle)
            k++;
        if (k == 0 && pool[EVPOOL_SIZE - 1].key != NULL) {
            /* Can't insert if the element is < the worst element we have
             * and there are no empty buckets. */
            continue;
        } else if (k < EVPOOL_SIZE && pool[k].key == NULL) {
            /* Inserting into empty position. No setup needed before insert. */
        } else {
            /* Inserting in the middle. Now k points to the first element
             * greater than the element to insert.  */
            if (pool[EVPOOL_SIZE - 1].key == NULL) {
                /* Free space on the right? Insert at k shifting
                 * all the elements from k to end to the right. */

                /* Save SDS before overwriting. */
                sds cached = pool[EVPOOL_SIZE - 1].cached;
                memmove(pool + k + 1, pool + k,
                        sizeof(pool[0]) * (EVPOOL_SIZE - k - 1));
                pool[k].cached = cached;
            } else {
                /* No free space on right? Insert at k-1 */
                k--;
                /* Shift all elements on the left of k (included) to the
                 * left, so we discard the element with smaller idle time. */
                sds cached = pool[0].cached; /* Save SDS before overwriting. */
                if (pool[0].key != pool[0].cached) sdsfree(pool[0].key);
                memmove(pool, pool + 1, sizeof(pool[0]) * k);
                pool[k].cached = cached;
            }
        }

        /* Try to reuse the cached SDS string allocated in the pool entry,
         * because allocating and deallocating this object is costly
         * (according to the profiler, not my fantasy. Remember:
         * premature optimizbla bla bla bla. */
        int klen = sdslen(key);
        if (klen > EVPOOL_CACHED_SDS_SIZE) {
            pool[k].key = sdsdup(key);
        } else {
            memcpy(pool[k].cached, key, klen + 1);
            sdssetlen(pool[k].cached, klen);
            pool[k].key = pool[k].cached;
        }
        pool[k].idle = idle;
        pool[k].dbid = dbid;
    }
}

/*
 * LFU复用了LRU字段，高16位是以1分钟为精度的UNIX时间戳。低8位是一个对数计数器，提供访问频率。
 *
 * Redis中LFU(Least Frequently Used)的实现原理如下:
1.每个key都维护一个频次计数器frequency,初始为1。
2.有一个全局的最小频次变量minfreq,初始为1。
3.当一个key被访问时,其frequency自增1。
4.当访问次数达到预设阈值后,触发下面操作:
    减半频次计数器:将所有key的frequency除以2。
    更新minfreq为减半后最小的frequency。
    删除frequency小于minfreq的部分key。
5.重复以上步骤,使得访问频率最少的key会被删除。
6.当内存不足时,会提前触发上述操作,删除更多的低频key。
所以Redis通过频次计数器和全局最小频次的配合,实现了LFU缓存淘汰策略。
它期望删除访问频率最低的key,以保留热点数据,提高缓存命中率。
 * */

/* 返回当前时间（以分钟为单位），仅取最低有效 16 位。 返回的时间适合存储为 LDT（最后递减时间）以供 LFU 实现。 */
unsigned long LFUGetTimeInMinutes(void) {
    return (server.unixtime / 60) & 65535;
}

/* 给定对象的上次访问时间，计算自上次访问以来经过的最小分钟数。 */
unsigned long LFUTimeElapsed(unsigned long ldt) {
    unsigned long now = LFUGetTimeInMinutes();
    if (now >= ldt) return now - ldt;
    return 65535 - ldt + now;
}

/* counter越大, 增长越困难 */
uint8_t LFULogIncr(uint8_t counter) {
    if (counter == 255) return 255; //判断计数器当前值为255,已经达到最大,直接返回255。
    double r = (double) rand() / RAND_MAX; //生成一个0-1随机数r。
    double baseval = counter - LFU_INIT_VAL; //计算出计数器距离初始值LFU_INIT_VAL的基础值baseval。
    if (baseval < 0) baseval = 0;
    /*
     * 阈值p的大小就决定了访问次数增加的难度.阈值p越小, r<p的概率越小.
     * 阈值p的大小,由baseval 和 server.lfu_log_factor决定, 两者越大, p越小.
     * baseval是访问次数的多少,通过这种随机增长方式,随着计数器值的增大,其增长速度会越来越慢。
     * 比如计数器在100时,增长速度会比在10时慢。这样实现了LFU算法的另一个关键点,使高频访问的key增长到一个稳定区间,避免单调增长导致的问题。
     * */
    double p = 1.0 / (baseval * server.lfu_log_factor + 1);
    if (r < p) counter++;
    return counter;
}

/* 衰减访问次数,LFU的访问频率需要考虑到键值对是在多长时间段内发生的 */
unsigned long LFUDecrAndReturn(robj *o) {
    //从key的lru字段中取出最后访问时间ldt和计数器值counter。
    unsigned long ldt = o->lru >> 8;
    unsigned long counter = o->lru & 255;
    //计算从上次访问到现在过去了多少个周期num_periods。server.lfu_decay_time是预设的周期时长。
    unsigned long num_periods = server.lfu_decay_time ? LFUTimeElapsed(ldt) / server.lfu_decay_time : 0;
    if (num_periods)
        //如果过期周期数不为0,且大于计数器值,则计数器减去周期数。
        //如果减去周期数后计数器小于0,则直接设置为0。
        counter = (num_periods > counter) ? 0 : counter - num_periods;
    return counter;//返回减少后的计数器值。
}

/* ----------------------------------------------------------------------------
 * The external API for eviction: freeMemroyIfNeeded() is called by the
 * server when there is data to add in order to make space if needed.
 * --------------------------------------------------------------------------*/

/* We don't want to count AOF buffers and slaves output buffers as
 * used memory: the eviction should use mostly data size. This function
 * returns the sum of AOF and slaves buffer. */
size_t freeMemoryGetNotCountedMemory(void) {
    size_t overhead = 0;
    int slaves = listLength(server.slaves);

    if (slaves) {
        listIter li;
        listNode *ln;

        listRewind(server.slaves, &li);
        while ((ln = listNext(&li))) {
            client *slave = listNodeValue(ln);
            overhead += getClientOutputBufferMemoryUsage(slave);
        }
    }
    if (server.aof_state != AOF_OFF) {
        overhead += sdsalloc(server.aof_buf) + aofRewriteBufferSize();
    }
    return overhead;
}

/* Get the memory status from the point of view of the maxmemory directive:
 * if the memory used is under the maxmemory setting then C_OK is returned.
 * Otherwise, if we are over the memory limit, the function returns
 * C_ERR.
 *
 * The function may return additional info via reference, only if the
 * pointers to the respective arguments is not NULL. Certain fields are
 * populated only when C_ERR is returned:
 *
 *  'total'     total amount of bytes used.
 *              (Populated both for C_ERR and C_OK)
 *
 *  'logical'   the amount of memory used minus the slaves/AOF buffers.
 *              (Populated when C_ERR is returned)
 *
 *  'tofree'    the amount of memory that should be released
 *              in order to return back into the memory limits.
 *              (Populated when C_ERR is returned)
 *
 *  'level'     this usually ranges from 0 to 1, and reports the amount of
 *              memory currently used. May be > 1 if we are over the memory
 *              limit.
 *              (Populated both for C_ERR and C_OK)
 */
int getMaxmemoryState(size_t *total, size_t *logical, size_t *tofree, float *level) {
    size_t mem_reported, mem_used, mem_tofree;

    /* Check if we are over the memory usage limit. If we are not, no need
     * to subtract the slaves output buffers. We can just return ASAP. */
    mem_reported = zmalloc_used_memory(); //计算已使用的内存量
    if (total) *total = mem_reported;

    /* We may return ASAP if there is no need to compute the level. */
    int return_ok_asap = !server.maxmemory || mem_reported <= server.maxmemory;
    if (return_ok_asap && !level) return C_OK;

    /* 将用于主从复制的复制缓冲区从已使用内存中移除 */
    mem_used = mem_reported;
    size_t overhead = freeMemoryGetNotCountedMemory();
    mem_used = (mem_used > overhead) ? mem_used - overhead : 0;

    /* Compute the ratio of memory usage. */
    if (level) {
        if (!server.maxmemory) {
            *level = 0;
        } else {
            *level = (float) mem_used / (float) server.maxmemory;
        }
    }

    if (return_ok_asap) return C_OK;

    /* Check if we are still over the memory limit. */
    if (mem_used <= server.maxmemory) return C_OK;

    /* 计算需要释放的内存量 */
    mem_tofree = mem_used - server.maxmemory;

    if (logical) *logical = mem_used;
    if (tofree) *tofree = mem_tofree;

    return C_ERR;
}

/* 内存释放, */
int freeMemoryIfNeeded(void) {
    /* By default replicas should ignore maxmemory
     * and just be masters exact copies. */
    if (server.masterhost && server.repl_slave_ignore_maxmemory) return C_OK;

    size_t mem_reported, mem_tofree, mem_freed;
    mstime_t latency, eviction_latency;
    long long delta;
    int slaves = listLength(server.slaves);

    if (clientsArePaused()) return C_OK;
    // 判断当前redis使用的内存是否超过了maxmemory配置的值
    if (getMaxmemoryState(&mem_reported, NULL, &mem_tofree, NULL) == C_OK)
        return C_OK;

    mem_freed = 0;

    if (server.maxmemory_policy == MAXMEMORY_NO_EVICTION)
        goto cant_free; /* 当前策略是不淘汰 */

    latencyStartMonitor(latency);
    // 持续释放,直到满足mem_tofree
    while (mem_freed < mem_tofree) {
        int j, k, i, keys_freed = 0;
        static unsigned int next_db = 0;
        sds evict_key = NULL;
        int evict_db_id;
        redisDb *db;
        dict *dict;
        dictEntry *de;

        // 处理LRU,LFU或者volatile-ttl
        if (server.maxmemory_policy & (MAXMEMORY_FLAG_LRU | MAXMEMORY_FLAG_LFU) ||
            server.maxmemory_policy == MAXMEMORY_VOLATILE_TTL) {
            struct evictionPoolEntry *pool = EvictionPoolLRU;

            while (evict_key == NULL) {
                unsigned long total_keys = 0, keys;

                for (i = 0; i < server.dbnum; i++) {
                    db = server.db + i;
                    //根据配置,选择使用全局hash表,还是过期时间的hash表
                    dict = (server.maxmemory_policy & MAXMEMORY_FLAG_ALLKEYS) ?
                           db->dict : db->expires;
                    if ((keys = dictSize(dict)) != 0) {
                        evictionPoolPopulate(i, dict, db->dict, pool);
                        total_keys += keys;
                    }
                }
                if (!total_keys) break; /* No keys to evict. */

                /* 从数组的最后一个key开始选择,如果选到的key不是空值,那么就是最终淘汰的key,
                 * 因为最后一个key的idle时间最大 */
                for (k = EVPOOL_SIZE - 1; k >= 0; k--) {
                    if (pool[k].key == NULL) continue; // 当前key为空值,则查找下一个key
                    evict_db_id = pool[k].dbid;

                    if (server.maxmemory_policy & MAXMEMORY_FLAG_ALLKEYS) {
                        de = dictFind(server.db[pool[k].dbid].dict, pool[k].key);
                    } else {
                        de = dictFind(server.db[pool[k].dbid].expires, pool[k].key);
                    }

                    /* Remove the entry from the pool. */
                    if (pool[k].key != pool[k].cached)
                        sdsfree(pool[k].key);
                    pool[k].key = NULL;
                    pool[k].idle = 0;

                    /* 如果当前key对应的键值对不为空,选择当前key为被淘汰的key */
                    if (de) {
                        evict_key = dictGetKey(de);
                        break;
                    } else {
                        /* Ghost... Iterate again. */
                    }
                }
            }
        }

            /* 如果使用的是随机淘汰算法 */
        else if (server.maxmemory_policy == MAXMEMORY_ALLKEYS_RANDOM ||
                 server.maxmemory_policy == MAXMEMORY_VOLATILE_RANDOM) {
            /* 要淘汰每个数据库,使用next_db作为静态变量,存储上次清理的db_id */
            for (i = 0; i < server.dbnum; i++) {
                j = (++next_db) % server.dbnum;
                db = server.db + j;
                dict = (server.maxmemory_policy == MAXMEMORY_ALLKEYS_RANDOM) ?
                       db->dict : db->expires;
                if (dictSize(dict) != 0) {
                    de = dictGetRandomKey(dict);
                    evict_key = dictGetKey(de);
                    evict_db_id = j;
                    break;
                }
            }
        }

        /* 删除前面选择的待淘汰键 */
        if (evict_key) {
            db = server.db + evict_db_id;
            robj *keyobj = createStringObject(evict_key, sdslen(evict_key));
            propagateExpire(db, keyobj, server.lazyfree_lazy_eviction);
            /* 计算当前使用的内存量 */
            delta = (long long) zmalloc_used_memory();
            latencyStartMonitor(eviction_latency);
            // 如果配置了惰性删除,则进行异步删除
            if (server.lazyfree_lazy_eviction)
                dbAsyncDelete(db, keyobj);
            else
                //否则就行同步删除
                dbSyncDelete(db, keyobj);
            latencyEndMonitor(eviction_latency);
            latencyAddSampleIfNeeded("eviction-del", eviction_latency);
            latencyRemoveNestedEvent(latency, eviction_latency);
            // //根据当前内存使用量计算数据删除前后释放的内存量
            delta -= (long long) zmalloc_used_memory();
            mem_freed += delta; // 更新已释放的内存量
            server.stat_evictedkeys++;
            notifyKeyspaceEvent(NOTIFY_EVICTED, "evicted", keyobj, db->id);
            decrRefCount(keyobj);
            keys_freed++;

            /* When the memory to free starts to be big enough, we may
             * start spending so much time here that is impossible to
             * deliver data to the slaves fast enough, so we force the
             * transmission here inside the loop. */
            if (slaves) flushSlavesOutputBuffers();

            /* Normally our stop condition is the ability to release
             * a fixed, pre-computed amount of memory. However when we
             * are deleting objects in another thread, it's better to
             * check, from time to time, if we already reached our target
             * memory, since the "mem_freed" amount is computed only
             * across the dbAsyncDelete() call, while the thread can
             * release the memory all the time. */
            if (server.lazyfree_lazy_eviction && !(keys_freed % 16)) {
                if (getMaxmemoryState(NULL, NULL, NULL, NULL) == C_OK) {
                    /* Let's satisfy our stop condition. */
                    mem_freed = mem_tofree;
                }
            }
        }

        if (!keys_freed) {
            latencyEndMonitor(latency);
            latencyAddSampleIfNeeded("eviction-cycle", latency);
            goto cant_free; /* nothing to free... */
        }
    }
    latencyEndMonitor(latency);
    latencyAddSampleIfNeeded("eviction-cycle", latency);
    return C_OK;

    cant_free:
    /* We are here if we are not able to reclaim memory. There is only one
     * last thing we can try: check if the lazyfree thread has jobs in queue
     * and wait... */
    while (bioPendingJobsOfType(BIO_LAZY_FREE)) {
        if (((mem_reported - zmalloc_used_memory()) + mem_freed) >= mem_tofree)
            break;
        usleep(1000);
    }
    return C_ERR;
}

int freeMemoryIfNeededAndSafe(void) {
    // 必须没有处于超时状态的脚本
    if (server.lua_timedout || server.loading) return C_OK;
    return freeMemoryIfNeeded();
}
