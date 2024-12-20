public class Main {
    public static void main(String[] args) {
        SystemArrayCopyTest.testSystemArrayCopyErrors();
    }
}

class SystemArrayCopyTest {
    static void testSystemArrayCopyErrors() {
        // Null source array
        try {
            System.arraycopy(null, 0, new int[5], 0, 5);
        } catch (NullPointerException e) {
            System.out.print("a");
        }

        // Null destination array
        try {
            System.arraycopy(new int[5], 0, null, 0, 5);
        } catch (NullPointerException e) {
            System.out.print("b");
        }

        // Source array out of bounds
        try {
            int[] src = new int[5];
            int[] dest = new int[5];
            System.arraycopy(src, -1, dest, 0, 5);
        } catch (IndexOutOfBoundsException e) {
            System.out.print("c");
        }

        // Destination array out of bounds
        try {
            int[] src = new int[5];
            int[] dest = new int[5];
            System.arraycopy(src, 0, dest, -1, 5);
        } catch (IndexOutOfBoundsException e) {
            System.out.print("d");
        }

        // Length exceeds source array bounds
        try {
            int[] src = new int[5];
            int[] dest = new int[5];
            System.arraycopy(src, 0, dest, 0, 10);
        } catch (IndexOutOfBoundsException e) {
            System.out.print("e");
        }

        // Length exceeds destination array bounds
        try {
            int[] src = new int[5];
            int[] dest = new int[5];
            System.arraycopy(src, 0, dest, 2, 5);
        } catch (IndexOutOfBoundsException e) {
            System.out.print("f");
        }

        try {
            Object[] src = new String[5];
            Integer[] dest = new Integer[5];
            System.arraycopy(src, 0, dest, 0, 5);
        } catch (ArrayStoreException e) {
            System.out.print("this is ok");
        }

        Integer[] dest = new Integer[5];

        // Source and destination arrays of different types
        try {
            Object[] src = new String[5];
            src[1] = "hello";
            dest[0] = 10;
            System.arraycopy(src, 0, dest, 0, 5);
        } catch (ArrayStoreException e) {
            System.out.print("g");
        }

        if (dest[0] == null)
            System.out.print("h");

        // Success cases
        // Overlapping with self
        int[] arr = new int[5];
        arr[0] = 1;
        arr[1] = 2;
        arr[2] = 3;
        arr[3] = 4;
        arr[4] = 5;
        System.arraycopy(arr, 0, arr, 1, 4);
        if (arr[0] == 1 && arr[1] == 1 && arr[2] == 2 && arr[3] == 3 && arr[4] == 4)
            System.out.print("i");

        // short[] to short[]
        short[] srcshort = new short[5];
        short[] destshort = new short[5];
        srcshort[0] = 1;
        srcshort[1] = 2;
        srcshort[2] = 3;
        srcshort[3] = 4;
        srcshort[4] = 5;
        System.arraycopy(srcshort, 2, destshort, 0, 3);
        if (destshort[0] == 3 && destshort[1] == 4 && destshort[2] == 5)
            System.out.print("j");
            
        // byte[] to byte[]
        byte[] srcbyte = new byte[5];
        byte[] destbyte = new byte[5];
        srcbyte[0] = 1;
        srcbyte[1] = 2;
        srcbyte[2] = 3;
        srcbyte[3] = 4;
        srcbyte[4] = 5;
        System.arraycopy(srcbyte, 2, destbyte, 0, 3);
        if (destbyte[0] == 3 && destbyte[1] == 4 && destbyte[2] == 5)
            System.out.print("k");
            
        // int[] to int[]
        int[] srcint = new int[5];
        int[] destint = new int[5];
        srcint[0] = 1;
        srcint[1] = 2;
        srcint[2] = 3;
        srcint[3] = 4;
        srcint[4] = 5;
        System.arraycopy(srcint, 2, destint, 0, 3);
        if (destint[0] == 3 && destint[1] == 4 && destint[2] == 5)
            System.out.print("l");
            
        // long[] to long[]
        long[] srclong = new long[5];
        long[] destlong = new long[5];
        srclong[0] = 1;
        srclong[1] = 2;
        srclong[2] = 3;
        srclong[3] = 4;
        srclong[4] = 5;
        System.arraycopy(srclong, 2, destlong, 0, 3);
        if (destlong[0] == 3 && destlong[1] == 4 && destlong[2] == 5)
            System.out.print("j");
            
        // char[] to char[]
        char[] srcchar = new char[5];
        char[] destchar = new char[5];
        srcchar[0] = 1;
        srcchar[1] = 2;
        srcchar[2] = 3;
        srcchar[3] = 4;
        srcchar[4] = 5;
        System.arraycopy(srcchar, 2, destchar, 0, 3);
        if (destchar[0] == 3 && destchar[1] == 4 && destchar[2] == 5)
            System.out.print("k");
            
        // float[] to float[]
        float[] srcfloat = new float[5];
        float[] destfloat = new float[5];
        srcfloat[0] = 1;
        srcfloat[1] = 2;
        srcfloat[2] = 3;
        srcfloat[3] = 4;
        srcfloat[4] = 5;
        System.arraycopy(srcfloat, 2, destfloat, 0, 3);
        if (destfloat[0] == 3 && destfloat[1] == 4 && destfloat[2] == 5)
            System.out.print("l");
            
        // double[] to double[]
        double[] srcdouble = new double[5];
        double[] destdouble = new double[5];
        srcdouble[0] = 1;
        srcdouble[1] = 2;
        srcdouble[2] = 3;
        srcdouble[3] = 4;
        srcdouble[4] = 5;
        System.arraycopy(srcdouble, 2, destdouble, 0, 3);
        if (destdouble[0] == 3 && destdouble[1] == 4 && destdouble[2] == 5)
            System.out.print("m");
    }
}
