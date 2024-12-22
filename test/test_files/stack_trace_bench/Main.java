
class CustomException extends RuntimeException {
    public CustomException(String message) {
        super(message);
    }
}

public class Main {
    static void method1(int depth) {
        method2(depth + 1);
    }

    static void method2(int depth) {
        method3(depth + 1);
    }

    static void method3(int depth) {
        if (depth > 500) {
            throw new CustomException("Depth is too high");
        }
        method1(depth + 1);
    }

    public static void main(String[] args) {
        System.out.println("Hello, World!");

        for (int i = 0; i < 5000; i++) {
            try {
                method1(0);
            } catch (CustomException e) {

            }
        }
    }
}