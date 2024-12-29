import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class ReflectionMethod {
    long twoLongs(long a, long b) {
        return a + b;
    }

    Object identity(Object o) {
        return o;
    }

    void throwsException() throws IllegalAccessException {
        throw new IllegalAccessException();
    }

    public static void main(String[] args) throws NoSuchMethodException, InvocationTargetException, IllegalAccessException {
        Method twoLongs = ReflectionMethod.class.getDeclaredMethod("twoLongs", long.class, long.class);
        try {
            System.out.println("Reached!");
            twoLongs.invoke(null, 10);
        } catch (NullPointerException e) {
            System.out.println("Reached2!");
            System.out.print("a");
        }
        System.out.println("Reached3!");

        try {
            twoLongs.invoke(new ReflectionMethod(), 10);
        } catch (IllegalArgumentException e) {
        System.out.println("Reached4!");
            System.out.print("b");
        }
        try {
            Long p = (Long) twoLongs.invoke(new ReflectionMethod(), 10, 20);
            System.out.print(p);
        } catch (IllegalAccessException e) {
            System.out.print("Not this!");
        }
        try {
            long p = (long) twoLongs.invoke(new ReflectionMethod(), 10, 20);
            System.out.print(p);
        } catch (IllegalAccessException e) {
            System.out.print("Not this!");
        }

        Method throwsException = ReflectionMethod.class.getDeclaredMethod("throwsException");
        try {
            throwsException.invoke(new ReflectionMethod());
        } catch (InvocationTargetException e) {
            System.out.print("e");
        }

        Method identity = ReflectionMethod.class.getDeclaredMethod("identity", Object.class);
        try {
            Object o = identity.invoke(new ReflectionMethod(), "f");
            System.out.print(o);
        } catch (IllegalAccessException e) {
            System.out.print("Not this!");
        }
    }
}