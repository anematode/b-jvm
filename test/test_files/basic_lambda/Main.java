public class Main {
    String s = "hello from l";

    public static void main(String[] args) {
        new Main().enjoyment();
    }

    void enjoyment() {
        ((Runnable) () -> {
            System.out.println("Hello from lambda!");
            System.out.println("Printing: " + s);
        }).run();
    }
}