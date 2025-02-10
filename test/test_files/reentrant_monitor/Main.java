import java.math.BigInteger;

public class Main {
    public static void main(String[] args) {
        ReentrantFactorialCalculator calc = new ReentrantFactorialCalculator();
        System.out.println(calc);
        BigInteger result = calc.factorial(5);
        System.out.printf("5! = %s\n", result);

        System.out.println(calc);
        result = calc.factorial(3);
        System.out.printf("3! = %s\n", result);
        System.out.println(calc);

        result = calc.factorial(20);
        System.out.printf("100! = %s\n", result);
        System.out.println(calc);

        System.out.println("caching correctly: " + (calc.factorial(1000) == calc.factorial(1000)));
    }
}