import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

public class Main {
    public static void main(String[] args) {
        String[] sa = new String[100];
        sa[10] = "expected";
        VarHandle avh = MethodHandles.arrayElementVarHandle(String[].class);
        boolean r = avh.compareAndSet(sa, 10, "expected", "new");

        System.out.println(r);
        System.out.println(sa[10]);
    }
}