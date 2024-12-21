public class Main {
    public static void main(String[] args) {
        testNewArray();
        testANewArray();
        testMultiANewArray();
    }

    private static void testNewArray() {
        try {
            int[] intArray = new int[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("a");
            }
        }

        try {
            double[] doubleArray = new double[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("b");
            }
        }

        try {
            boolean[] booleanArray = new boolean[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("c");
            }
        }
    }

    private static void testANewArray() {
        try {
            String[] stringArray = new String[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("d");
            }
        }

        try {
            Object[] objectArray = new Object[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("e");
            }
        }

        try {
            Integer[] integerArray = new Integer[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("f");
            }
        }
    }

    private static void testMultiANewArray() {
        try {
            int[][] multiIntArray = new int[-1][5];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("g");
            }
        }

        try {
            int[][] multiIntArray = new int[5][-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("h");
            }
        }

        try {
            String[][] multiStringArray = new String[-1][5];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("i");
            }
        }

        try {
            String[][] multiStringArray = new String[5][-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("j");
            }
        }
    }
}
