function t(n, t) {
    var r = (65535 & n) + (65535 & t), u = (n >> 16) + (t >> 16) + (r >> 16);
    return u << 16 | 65535 & r
}

function r(n, t) {
    return n << t | n >>> 32 - t
}

function u(n, u, e, o, c, f) {
    return t(r(t(t(u, n), t(o, f)), c), e)
}

function e(n, t, r, e, o, c, f) {
    return u(t & r | ~t & e, n, t, o, c, f)
}

function o(n, t, r, e, o, c, f) {
    return u(t & e | r & ~e, n, t, o, c, f)
}

function c(n, t, r, e, o, c, f) {
    return u(t ^ r ^ e, n, t, o, c, f)
}

function f(n, t, r, e, o, c, f) {
    return u(r ^ (t | ~e), n, t, o, c, f)
}

function i(n, r) {
    n[r >> 5] |= 128 << r % 32, n[(r + 64 >>> 9 << 4) + 14] = r;
    var u, i, a, h, g, l = 1732584193, d = -271733879, v = -1732584194, C = 271733878;
    for (u = 0; u < n.length; u += 16) i = l, a = d, h = v, g = C, l = e(l, d, v, C, n[u], 7, -680876936), C = e(C, l, d, v, n[u + 1], 12, -389564586), v = e(v, C, l, d, n[u + 2], 17, 606105819), d = e(d, v, C, l, n[u + 3], 22, -1044525330), l = e(l, d, v, C, n[u + 4], 7, -176418897), C = e(C, l, d, v, n[u + 5], 12, 1200080426), v = e(v, C, l, d, n[u + 6], 17, -1473231341), d = e(d, v, C, l, n[u + 7], 22, -45705983), l = e(l, d, v, C, n[u + 8], 7, 1770035416), C = e(C, l, d, v, n[u + 9], 12, -1958414417), v = e(v, C, l, d, n[u + 10], 17, -42063), d = e(d, v, C, l, n[u + 11], 22, -1990404162), l = e(l, d, v, C, n[u + 12], 7, 1804603682), C = e(C, l, d, v, n[u + 13], 12, -40341101), v = e(v, C, l, d, n[u + 14], 17, -1502002290), d = e(d, v, C, l, n[u + 15], 22, 1236535329), l = o(l, d, v, C, n[u + 1], 5, -165796510), C = o(C, l, d, v, n[u + 6], 9, -1069501632), v = o(v, C, l, d, n[u + 11], 14, 643717713), d = o(d, v, C, l, n[u], 20, -373897302), l = o(l, d, v, C, n[u + 5], 5, -701558691), C = o(C, l, d, v, n[u + 10], 9, 38016083), v = o(v, C, l, d, n[u + 15], 14, -660478335), d = o(d, v, C, l, n[u + 4], 20, -405537848), l = o(l, d, v, C, n[u + 9], 5, 568446438), C = o(C, l, d, v, n[u + 14], 9, -1019803690), v = o(v, C, l, d, n[u + 3], 14, -187363961), d = o(d, v, C, l, n[u + 8], 20, 1163531501), l = o(l, d, v, C, n[u + 13], 5, -1444681467), C = o(C, l, d, v, n[u + 2], 9, -51403784), v = o(v, C, l, d, n[u + 7], 14, 1735328473), d = o(d, v, C, l, n[u + 12], 20, -1926607734), l = c(l, d, v, C, n[u + 5], 4, -378558), C = c(C, l, d, v, n[u + 8], 11, -2022574463), v = c(v, C, l, d, n[u + 11], 16, 1839030562), d = c(d, v, C, l, n[u + 14], 23, -35309556), l = c(l, d, v, C, n[u + 1], 4, -1530992060), C = c(C, l, d, v, n[u + 4], 11, 1272893353), v = c(v, C, l, d, n[u + 7], 16, -155497632), d = c(d, v, C, l, n[u + 10], 23, -1094730640), l = c(l, d, v, C, n[u + 13], 4, 681279174), C = c(C, l, d, v, n[u], 11, -358537222), v = c(v, C, l, d, n[u + 3], 16, -722521979), d = c(d, v, C, l, n[u + 6], 23, 76029189), l = c(l, d, v, C, n[u + 9], 4, -640364487), C = c(C, l, d, v, n[u + 12], 11, -421815835), v = c(v, C, l, d, n[u + 15], 16, 530742520), d = c(d, v, C, l, n[u + 2], 23, -995338651), l = f(l, d, v, C, n[u], 6, -198630844), C = f(C, l, d, v, n[u + 7], 10, 1126891415), v = f(v, C, l, d, n[u + 14], 15, -1416354905), d = f(d, v, C, l, n[u + 5], 21, -57434055), l = f(l, d, v, C, n[u + 12], 6, 1700485571), C = f(C, l, d, v, n[u + 3], 10, -1894986606), v = f(v, C, l, d, n[u + 10], 15, -1051523), d = f(d, v, C, l, n[u + 1], 21, -2054922799), l = f(l, d, v, C, n[u + 8], 6, 1873313359), C = f(C, l, d, v, n[u + 15], 10, -30611744), v = f(v, C, l, d, n[u + 6], 15, -1560198380), d = f(d, v, C, l, n[u + 13], 21, 1309151649), l = f(l, d, v, C, n[u + 4], 6, -145523070), C = f(C, l, d, v, n[u + 11], 10, -1120210379), v = f(v, C, l, d, n[u + 2], 15, 718787259), d = f(d, v, C, l, n[u + 9], 21, -343485551), l = t(l, i), d = t(d, a), v = t(v, h), C = t(C, g);
    return [l, d, v, C]
}

function a(n) {
    var t, r = "";
    for (t = 0; t < 32 * n.length; t += 8) r += String.fromCharCode(n[t >> 5] >>> t % 32 & 255);
    return r
}

function h(n) {
    var t, r = [];
    for (r[(n.length >> 2) - 1] = void 0, t = 0; t < r.length; t += 1) r[t] = 0;
    for (t = 0; t < 8 * n.length; t += 8) r[t >> 5] |= (255 & n.charCodeAt(t / 8)) << t % 32;
    return r
}

function g(n) {
    return a(i(h(n), 8 * n.length))
}

function l(n, t) {
    var r, u, e = h(n), o = [], c = [];
    for (o[15] = c[15] = void 0, e.length > 16 && (e = i(e, 8 * n.length)), r = 0; r < 16; r += 1) o[r] = 909522486 ^ e[r], c[r] = 1549556828 ^ e[r];
    return u = i(o.concat(h(t)), 512 + 8 * t.length), a(i(c.concat(u), 640))
}

function d(n) {
    var t, r, u = "0123456789abcdef", e = "";
    for (r = 0; r < n.length; r += 1) t = n.charCodeAt(r), e += u.charAt(t >>> 4 & 15) + u.charAt(15 & t);
    return e
}

function v(n) {
    return unescape(encodeURIComponent(n))
}

function C(n) {
    return g(v(n))
}

function A(n) {
    return d(C(n))
}

function m(n, t) {
    return l(v(n), v(t))
}

function s(n, t) {
    return d(m(n, t))
}

function md5(n, t, r) {
    return t ? r ? m(t, n) : s(t, n) : r ? C(n) : A(n)
}

function getcdata(y) {
    if ("string" == typeof y && "" != y) {
        var r = 0;
        y = y.replace(/&quot;/g, '"'), y = y.replace(/&apos;/g, "'");
        var n;
        try {
            n = eval('(' + y + ')')
        } catch (e) {
        }
        ;
        if ("object" == typeof n && "string" == typeof n.randstr && ("string" == typeof n.M || "number" == typeof n.M) && "string" == typeof n.ans) {
            n.ans = n.ans.toLowerCase(), n.M = parseInt(n.M);
            for (var s = 0; s < n.M && s < 1e3; s++) {
                var i = n.randstr + s, c = md5(i);
                if (n.ans == c.toLowerCase()) {
                    r = s;
                    return r;
                }
            }
        }
    }
};

function rnd() {
    return Math.floor(1e6 * Math.random())
};

function random() {
    return +new Date();
};


