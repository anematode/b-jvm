
class Eggnog {
    static {
        System.out.print("I should not be printed!");
    }
}

public class Main {
    public static void main(String[] args) {
        String a = null;
        try {
            System.out.print(a.length());
        } catch (NullPointerException e) {
            System.out.print("a");
        }
        try {
            System.out.print(a.length());
        } catch (NullPointerException e) {
            System.out.print("b");
        }
        int[] b = null;
        try {
            System.out.print(b[0]);
        } catch (NullPointerException e) {
            System.out.print("c");
        }

        Eggnog e = null;
        try {
            System.out.print(e.toString());
        } catch (NullPointerException ex) {
            System.out.print("d");
        }

        short[] shortArray = null;
        try {
            System.out.print(shortArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("e");
        }
        
        try {
            shortArray[0] = 1;
        } catch (NullPointerException ex) {
            System.out.print("f");
        }
        
        long[] longArray = null;
        try {
            System.out.print(longArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("g");
        }
        
        try {
            longArray[0] = 1;
        } catch (NullPointerException ex) {
            System.out.print("h");
        }
        
        double[] doubleArray = null;
        try {
            System.out.print(doubleArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("i");
        }
        
        try {
            doubleArray[0] = 1;
        } catch (NullPointerException ex) {
            System.out.print("j");
        }
        
        float[] floatArray = null;
        try {
            System.out.print(floatArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("k");
        }
        
        try {
            floatArray[0] = 1;
        } catch (NullPointerException ex) {
            System.out.print("l");
        }
        
        byte[] byteArray = null;
        try {
            System.out.print(byteArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("m");
        }
        
        try {
            byteArray[0] = 1;
        } catch (NullPointerException ex) {
            System.out.print("n");
        }
        
        char[] charArray = null;
        try {
            System.out.print(charArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("o");
        }
        
        try {
            charArray[0] = 'a';
        } catch (NullPointerException ex) {
            System.out.print("p");
        }

        Object[] objectArray = null;
        try {
            System.out.print(objectArray[0]);
        } catch (NullPointerException ex) {
            System.out.print("q");
        }

        try {
            objectArray[0] = new Object();
        } catch (NullPointerException ex) {
            System.out.print("r");
        }
    }
}