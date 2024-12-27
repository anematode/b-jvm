import java.math.BigDecimal;
import java.math.RoundingMode;

public class Main {
    static int p = 10;
    public static void main(String[] args) {
        long p = 0;
        for (int i = 0; i < 50; ++i) {
        System.out.println(divWord(280633750, -569676998));
        }
    }

    final static long LONG_MASK = 0xffffffffL;

    static long divWord(long n, int d) {
       int r = 280633750;
       int q = 0;
       long dLong = 3725290298L;
       while (r >= dLong) {
           r -= dLong;
           q++;
       }
       return dLong;
   }
}

class BigDecimalTest {
    static void error() {
        throw new RuntimeException("HI");
    }


}
