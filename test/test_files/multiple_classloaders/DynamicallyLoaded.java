public class DynamicallyLoaded {
    static {
        System.out.println("I was initialized!");
    }
    public static void main(String[] args) throws Exception {
        System.out.println("Hello, world!");

        Class<?> clazz = Class.forName("TransitivelyLoaded");
        // Create instance
        Object obj = clazz.newInstance();
    }
}