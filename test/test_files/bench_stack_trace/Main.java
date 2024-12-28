public class Main {
    private static final int ITERATIONS = 10_000;      // Number of times to run
    private static final int RECURSION_DEPTH = 500;    // Depth of recursion

    public static void main(String[] args) {
        for (int i = 0; i < ITERATIONS; i++) {
            try {
                causeDeepRecursion(RECURSION_DEPTH);
            } catch (Exception e) {
                // We are only catching to force the JVM to generate a stack trace.
                // The performance cost is in creating the stack trace, not in handling it.
            }
        }
    }

    private static void causeDeepRecursion(int depth) throws Exception {
        if (depth <= 0) {
            throw new Exception("Intentional exception to generate stack trace.");
        }
        causeDeepRecursion(depth - 1);
    }
}