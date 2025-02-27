import java.math.BigDecimal;
import java.math.RoundingMode;

public class Main {
    public static void main(String[] args) {
        BigDecimalTest.testBigDecimalOperations(Integer.parseInt(args[0]));
    }
}

class BigDecimalTest {
    static void testBigDecimalOperations(int count) {
        for (int i = 0; i < count; ++i) {
        BigDecimal a = new BigDecimal("10.5");
        BigDecimal b = new BigDecimal("2.3");
        BigDecimal result = a.add(b);
        System.out.println("10.5 + 2.3 = " + result);

        result = a.subtract(b);
        System.out.println("10.5 - 2.3 = " + result);

        result = a.multiply(b);
        System.out.println("10.5 * 2.3 = " + result);

        BigDecimal c = new BigDecimal("10");
        BigDecimal d = new BigDecimal("3");
        result = c.divide(d, 2, RoundingMode.HALF_UP);
        System.out.println("10 / 3 (rounded to 2 decimals) = " + result);

        BigDecimal e = new BigDecimal("10.500");
        System.out.println("10.5 equals 10.500: " + a.equals(e));
        System.out.println("10.5 compareTo 10.500: " + a.compareTo(e));

        BigDecimal f = new BigDecimal("3.141592653589793238462643383279502884179");
        System.out.println("pi * pi is " + f.multiply(f));
        }
    }
}
