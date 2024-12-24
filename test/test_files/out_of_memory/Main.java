import java.util.ArrayList;

public class Main {
    public static void main(String[] args) {
        try {
            new Main().egg(0);
        } catch (OutOfMemoryError e) {
            e.printStackTrace();
        }
    }

    void egg(int depth) {
        if (depth > 300) {
            ArrayList<Integer> list = new ArrayList<>();
            while (true) {
                list.add(1);
            }
        }
        egg(depth + 1);
    }
}