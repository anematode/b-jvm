public class ManuallyThrown {
    public static void main(String[] args) {
        try {
            cow();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    static void cow() {
        throw new NullPointerException();
    }
}
