
public class Main {
    public static void main(String[] args) {
        // Conversion to int saturates
        float values[] = {Float.MAX_VALUE, Float.MIN_VALUE, Float.POSITIVE_INFINITY, Float.NEGATIVE_INFINITY, Float.NaN};
        double values2[] = {Double.MAX_VALUE, Double.MIN_VALUE, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY, Double.NaN};
        for (float f : values) {
            System.out.println(f + " -> " + (int) f);
        }
        for (float f : values) {
            System.out.println(f + " -> " + (long) f + "L");
        }
        for (double d : values2) {
            System.out.println(d + " -> " + (int) d);
        }
        for (double d : values2) {
            System.out.println(d + " -> " + (long) d + "L");
        }
    }
}