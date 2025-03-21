import java.util.ArrayList;

public class Main {
    static int p = 10;
    public static void main(String[] args) {
        System.out.println("Hello from Java!\n");
        for (int i = 0; i < 10; ++i) {
            System.out.print(i);
            System.out.print(' ');
        }
        try {
            throwz();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public static ArrayList<Integer> add(int a, int b) {
        ArrayList<Integer> lst = new ArrayList<>();
        lst.add(a);
        lst.add(b);
        return lst;
    }

    static void throwz() {
        throw new RuntimeException("Oops!");
    }
}
