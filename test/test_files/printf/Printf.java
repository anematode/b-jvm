
public class Printf {
    public static void main(String[] args) {
        System.out.println("Hello!");
        System.out.printf("Hello, %s!%n", "world");
        System.out.printf("Hello, %d! The answer is %d. The quick brown %s %s over the lazy dog.%n", 42, 42L, "fox", "jumps");
    }
}