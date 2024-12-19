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
                System.out.print("k");
            }
        }

        try {
            double[] doubleArray = new double[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }

        try {
            boolean[] booleanArray = new boolean[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }
    }

    private static void testANewArray() {
        System.out.print("k");
        try {
            String[] stringArray = new String[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }

        try {
            Object[] objectArray = new Object[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }

        try {
            Integer[] integerArray = new Integer[-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }
    }

    private static void testMultiANewArray() {
        System.out.print("k");
        try {
            int[][] multiIntArray = new int[-1][5];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }

        try {
            int[][] multiIntArray = new int[5][-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }

        try {
            String[][] multiStringArray = new String[-1][5];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }

        try {
            String[][] multiStringArray = new String[5][-1];
        } catch (NegativeArraySizeException e) {
            if (e.getMessage().contains("-1")) {
                System.out.print("k");
            }
        }
    }
}
