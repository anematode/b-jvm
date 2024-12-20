public class Main {
    public static void main(String[] args) {
        // We'll just print a known sequence of letters for each test.
        // 'a' for the first test, 'b' for the second, etc.

        // 1. log(1) = 0.0
        if (Math.log(1) == 0.0) System.out.print('a');

        // 2. log1p(0) = 0.0
        if (Math.log1p(0) == 0.0) System.out.print('b');

        // 3. expm1(0) = 0.0
        if (Math.expm1(0) == 0.0) System.out.print('c');

        // 4. exp(0) = 1.0
        if (Math.exp(0) == 1.0) System.out.print('d');

        // 5. sinh(0) = 0.0
        if (Math.sinh(0) == 0.0) System.out.print('e');

        // 6. tanh(0) = 0.0
        if (Math.tanh(0) == 0.0) System.out.print('f');

        // 7. cosh(0) = 1.0
        if (Math.cosh(0) == 1.0) System.out.print('g');

        // 8. atan(0) = 0.0
        if (Math.atan(0) == 0.0) System.out.print('h');

        // 9. atan2(0, 1) = 0.0
        if (Math.atan2(0, 1) == 0.0) System.out.print('i');

        // 10. sin(0) = 0.0
        if (Math.sin(0) == 0.0) System.out.print('j');

        // 11. asin(0) = 0.0
        if (Math.asin(0) == 0.0) System.out.print('k');

        // 12. cos(0) = 1.0
        if (Math.cos(0) == 1.0) System.out.print('l');

        // 13. acos(1) = 0.0
        if (Math.acos(1) == 0.0) System.out.print('m');

        // 14. log10(1) = 0.0
        if (Math.log10(1) == 0.0) System.out.print('n');

        // 15. tan(0) = 0.0
        if (Math.tan(0) == 0.0) System.out.print('o');

        // 16. IEEEremainder(5,2) = 1.0
        if (Math.IEEEremainder(5, 2) == 1.0) System.out.print('p');

        // 17. sqrt(1) = 1.0
        if (Math.sqrt(1) == 1.0) System.out.print('q');

        // 18. cbrt(8) = 2.0
        if (Math.cbrt(8) == 2.0) System.out.print('r');

        // 19. pow(2,3) = 8.0
        if (Math.pow(2, 3) == 8.0) System.out.print('s');

        // 20. hypot(3,4) = 5.0
        if (Math.hypot(3, 4) == 5.0) System.out.print('t');

        // 21. sin(+Infinity) = NaN
        if (Double.isNaN(Math.sin(Double.POSITIVE_INFINITY))) System.out.print('u');
    }
}