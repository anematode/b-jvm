import java.util.function.*;
public class Main {
    String s = "variable passed in";

    public static void main(String[] args) {
        new Main().enjoyment();
    }

    void enjoyment() {
        for (int i = 0; i < 2; ++i) {
        ((Runnable) () -> {
            System.out.println("Hello from lambda!");
            System.out.println(s);
        }).run();
        BiConsumer<Long,String> foo = (a, b) -> System.out.println(a + b);
        foo.accept(2L, "eggs");
    }
    }
}
