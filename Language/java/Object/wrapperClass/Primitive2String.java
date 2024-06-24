package Object.wrapperClass;

//包装类类型装换
//parseXXX(String s)和valueOf(String s) 把字符串类型转换成基本类型
public class Primitive2String {
    public static void main(String[] args) {
        var intStr = "123";
        var it1 = Integer.parseInt(intStr);
        var it2 = Integer.valueOf(intStr);
        System.out.println(it1);
        System.out.println(it2);
        System.out.println(Integer.compare(it1,it2));
        var floatStr = "4.56";
        var ft1 = Float.parseFloat(floatStr);
        var ft2 = Float.valueOf(floatStr);
        System.out.println(ft2);
        System.out.println(Float.compare(ft2, ft1)); // 两个Float对象进行比较，相等为0
        String boolStr = String.valueOf(true);
        System.out.println(boolStr.toUpperCase());
        System.out.println(String.valueOf("2.22f")+"test");

    }
}
