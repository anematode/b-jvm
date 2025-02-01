// This file is compiled with -g so that the local variable table is available
class A {
    int[][] b;
    public A() {
        b = new int[10][10];
        b[0] = null;
    }

    public A(int i) {
        b = null;
    }
}

public class ExtendedNPETests {
    public static void main(String[] args) {
        simpleTests();
    }

    public static A a() {
        return new A();
    }

    static void simpleTests() {
        try {
            int i = 0;
            int j = 0;
            a().b[i][j] = 99;
        } catch (NullPointerException e) {
            System.out.println(e.getMessage());
            assert e.getMessage().equals("Cannot store to int array because \"ExtendedNPETests.a().b[i]\" is null");
        }

        try {
            int p = 0;
            int j = 0;
            a().b[p][j] = 99;
        } catch (NullPointerException e) {
            assert e.getMessage().equals("Cannot store to int array because \"ExtendedNPETests.a().b[p]\" is null");
        }

        try {
            int i = 0;
            int j = 0;
            new A(1).b[0][j] = 99;
        } catch (NullPointerException e) {
            assert e.getMessage().equals("Cannot load from object array because \"b\" is null");
        }

        try {
            int i = 0;
            int j = 0;
            new A().b[0][j] = 99;
        } catch (NullPointerException e) {
            assert e.getMessage().equals("Cannot store to int array because \"b[0]\" is null");
        }
    }
}
