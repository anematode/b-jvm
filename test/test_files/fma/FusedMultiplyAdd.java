
public class FusedMultiplyAdd {
    public static void main(String[] args) {
        {
            double a = 0x1.0000000001p0;
            double b = 0x1.0000000001p0;
            double c = -0x1.0000000002p0;
            double result = Math.fma(a, b, c);
            System.out.println(result);
        }

        {
            float a = 0x1.00001p0f;
            float b = 0x1.00001p0f;
            float c = -0x1.00002p0f;
            float result = Math.fma(a, b, c);
            System.out.println(result);
        }
    }
}