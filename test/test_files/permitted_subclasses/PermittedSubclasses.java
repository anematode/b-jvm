
sealed interface I permits A, B /*, C*/ {
}
final class A implements I {
}
final class B implements I {
}
final class C /*implements I*/ {}

public class PermittedSubclasses {
    public static void main(String[] args) {
        for (Class<?> c : I.class.getPermittedSubclasses()) {
            System.out.println(c.getName());
        }
        try {
            C c = new C();  // will throw IncompatibleClassChangeError
        } catch (IncompatibleClassChangeError e) {
            System.out.println(e.getMessage());
        }
    }
}