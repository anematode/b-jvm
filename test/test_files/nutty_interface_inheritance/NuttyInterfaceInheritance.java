
interface A {
    void foo();
}

interface AA extends A {
    default void foo() {
        System.out.println("AA.foo");
    }
}

interface B extends A {
    default void foo() {
        System.out.println("B.foo");
    }
}

class C implements AA, B {

}