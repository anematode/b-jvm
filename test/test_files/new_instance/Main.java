
class Test {
    public Test(double a) {
        System.out.println("Test " + a);
    }

    public Test() {
        System.out.println("Test");
    }
}

public class Main {
    public static void main(String[] args) {
        // Use reflection to create a Test
        try {
            Class<?> clazz = Class.forName("Test");
            clazz.getConstructor(double.class).newInstance(3.14);
            clazz.getConstructor().newInstance();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}