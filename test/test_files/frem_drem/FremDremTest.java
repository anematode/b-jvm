public class FremDremTest {
    public static float do_frem(float x, float y) {
        return x % y;
    }

    public static double do_drem(double x, double y) {
        return x % y;
    }

    public static void main(String[] args) {
        System.out.println(do_frem(0.5f, 0.2f));
        System.out.println(do_drem(0.5, 0.2));
    }
}
