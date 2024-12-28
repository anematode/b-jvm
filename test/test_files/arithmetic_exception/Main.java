
public class Main {
    public static void main(String[] args) {
        try {
            System.out.print(1 / 0);
        } catch (ArithmeticException e) {
            System.out.print("a");
        }

        try {
            System.out.print(1L / 0L);
        } catch (ArithmeticException e) {
            System.out.print("b");
        }

        try {
            System.out.print(1 % 0);
        } catch (ArithmeticException e) {
            System.out.print("c");
        }

        try {
            System.out.print(1L % 0L);
        } catch (ArithmeticException e) {
            System.out.print("d");
        }
    }
}