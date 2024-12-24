
interface MathOperation {
    int operation(int a, long b);
}

class Cow {
    int value;
}

public class Main {
    public static void main(String[] args) {
        for (int i = 0; i < 1024; ++i) {
            MathOperation addition = (int a, long b) -> a + (int)b;
            System.out.println("10 + 5 = " + addition.operation(10, 5L));
            // Stream operation to reduce
            int[] numbers = {1, 2, 3, 4, 5};
            int sum = java.util.Arrays.stream(numbers).reduce(0, (a, b) -> a + b);
            System.out.println("Sum of numbers: " + sum);
            // Stream operation with ::
            java.util.Arrays.stream(numbers).forEach(System.out::println);
            // Lambda capturing variables from the environment
            Cow cow = new Cow();
            cow.value = 0;
            MathOperation increment = (int a, long b) -> {
                cow.value += b;
                return a + (int) b;
            };
            java.util.Arrays.stream(numbers).forEach(n -> System.out.println("Incremented: " + increment.operation(n, n)));
            int[] lots = new int[1000];
            for (int j = 0; j < 1000; ++j) lots[j] = j;
            sum = java.util.Arrays.stream(lots).reduce(0, (a, b) -> a + b);
            System.out.println("Sum of lots of numbers: " + sum);
            System.out.println("Cow value: " + cow.value);
        }
    }
}
