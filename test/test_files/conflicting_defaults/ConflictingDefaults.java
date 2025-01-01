// This tests an edge case in itable construction.
interface A {
    default void foo() {
        System.out.println("A.foo");
    }
    default A bar() {
        System.out.println("A.bar");
        return this;
    }
}

interface AA extends A {}

interface B {
    default void foo() {
        System.out.println("B.foo");
    }
    default A bar() {
        System.out.println("B.bar");
        return null;
    }
}

class C implements AA, B {
    // public void foo() { }
    public A bar() {
        AA.super.bar();
        B.super.bar();
        return this;
    }
}

public class ConflictingDefaults {
    public static void main(String[] args) {
        A c = new C().bar();
        try {
            c.foo(); // this crashes because there are two default implementations
        } catch (IncompatibleClassChangeError e) {
            e.printStackTrace();
        }
    }
}