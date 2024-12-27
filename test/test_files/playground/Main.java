import java.math.BigDecimal;
import java.math.RoundingMode;

public class Main {
    static int p = 10;
    public static void main(String[] args) {
        long p = 0;
        for (int i = 0; i < 100000; ++i) {
            testOperation();
        }
        System.out.println(p);
    }

    static int testOperation() {
        long a = 10;
        long b = 20;
        if (b < 15L) {
            p += 1;
        }
        return p;
    }
}

class BigDecimalTest {
    static void error() {
        throw new RuntimeException("HI");
    }


}
