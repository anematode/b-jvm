import java.math.BigDecimal;
import java.math.RoundingMode;

public class Main {
    public static void main(String[] args) {
        BigDecimalTest.testBigDecimalOperations();
    }
}

class BigDecimalTest {
    static void error() {
        throw new RuntimeException("HI");
    }

    static void testBigDecimalOperations() {
        for (int i = 0; i < 20; ++i) {
            System.out.println("Hello from Java!");
            System.setOut(System.out);
        }
    }
}
