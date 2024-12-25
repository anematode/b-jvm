import java.math.BigDecimal;
import java.math.RoundingMode;

public class Main {
    public static void main(String[] args) {
        long p = 0;
        for (int i = 0; i < 100; ++i)
            p += testOperation();
        System.out.println(p);
    }

    static int testOperation() {
        int a = 0;
        for (int i = 0; i < 1000; ++i) {
            a += i;
        }
        System.out.println("Bailing out!");
        return a;
    }
}

class BigDecimalTest {
    static void error() {
        throw new RuntimeException("HI");
    }


}
