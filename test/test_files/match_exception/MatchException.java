

sealed interface I permits A, B {
}
final class A implements I {
}
final class B implements I {
}
record R(I i) {
}

public class MatchException {
    public static void egg(R r) {
        switch (r) {
            case R(A a) -> System.out.println("A");
            case R(B b) -> System.out.println("B");
        }
    }
    public static void main(String[] args) {
        R r = new R(null);
        try {
            egg(r);
        } catch (java.lang.MatchException e) {
            System.out.println(e);
        }
        try {
            egg(null);
        } catch (java.lang.NullPointerException e) {
            System.out.println(e);
        }
        egg(new R(new A()));
        egg(new R(new B()));
    }
}