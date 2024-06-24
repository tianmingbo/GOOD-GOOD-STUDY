package src.main.java.dali.producer;

import org.apache.kafka.clients.producer.KafkaProducer;
import org.apache.kafka.clients.producer.ProducerConfig;
import org.apache.kafka.clients.producer.ProducerRecord;
import org.apache.kafka.common.serialization.StringSerializer;

import java.util.Properties;
import java.util.concurrent.ExecutionException;

/**
 * @author tianmingbo
 */
public class Producer {

    public static void main(String[] args) throws Exception {

        // 0 配置
        Properties prop = new Properties();
        // 连接集群 bootstrap.servers
        prop.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG, "http://127.0.0.1:9092");

        // 指定对应的key和value的序列化类型 key.serializer
        prop.put(ProducerConfig.KEY_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());
        prop.put(ProducerConfig.VALUE_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());
//        prop.put(ProducerConfig.ACKS_CONFIG, "all");
//        prop.put(ProducerConfig.RETRIES_CONFIG, 3);
//        prop.put(ProducerConfig.BATCH_SIZE_CONFIG, 323840);
//        prop.put(ProducerConfig.BUFFER_MEMORY_CONFIG, 33554432);

        // 1 创建kafka生产者对象
        // "" hello
        KafkaProducer<String, String> producer = new KafkaProducer<>(prop);

        // 2 发送数据
        for (int i = 0; i < 5; i++) {
            //构建消息实例
            producer.send(new ProducerRecord<>("tiandali", "dali" + i)).get();
            System.out.println("??/");
        }

        // 3 关闭资源*** 别忘喽释放资源
        producer.close();
    }
}
