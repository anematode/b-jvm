import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class Main {
    long twoLongs(long a, long b) {
        return a + b;
    }

    public static void main(String[] args) throws NoSuchMethodException, InvocationTargetException, IllegalAccessException {
        Method twoLongs = Main.class.getDeclaredMethod("twoLongs", long.class, long.class);

        twoLongs.invoke(null, 10);
    }
}