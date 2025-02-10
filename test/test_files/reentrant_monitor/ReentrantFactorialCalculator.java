import java.util.TreeMap;
import java.math.BigInteger;

public class ReentrantFactorialCalculator {
    private final TreeMap<Integer, BigInteger> cache = new TreeMap<>();

    public BigInteger factorial(int n) {
        synchronized (this) {
            if (cache.containsKey(n)) {
                return cache.get(n);
            }

            if (n == 0) {
                return BigInteger.ONE;
            }

            BigInteger result = BigInteger.valueOf(n).multiply(factorial(n - 1));
            cache.put(n, result);
            return result;
        }
    }

    public String toString() {
        return "ReentrantFactorialCalculator: " + cache.toString();
    }
}