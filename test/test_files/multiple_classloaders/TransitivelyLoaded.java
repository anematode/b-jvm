public class TransitivelyLoaded {
    static {
        System.out.println("Hey!");
    }

    TransitivelyLoaded2 t = new TransitivelyLoaded2();

    final int x;
    public TransitivelyLoaded() {
        x = 10;
    }
}