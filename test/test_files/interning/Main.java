public class Main {
    public static String egg() {
        return Integer.toString(10000);
    }
    public static void main(String[] args){
        System.out.println("a" == "b");
        System.out.println(new String("a") == "a");
        System.out.println("a" == "a");
        System.out.println("a".intern() == "a");
        System.out.println(new String("a").intern() == "a");

        String s = egg();
        System.out.println("10000" == s);
        System.out.println("10000" == s.intern());
        System.out.println(s == s.intern());
    }
}

