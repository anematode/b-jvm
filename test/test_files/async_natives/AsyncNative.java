public class AsyncNative {
    public static native int myYield(int input);
    public static void doAsyncThing() {
        System.out.println(myYield(2));
        System.out.println(myYield(4));
    }
}