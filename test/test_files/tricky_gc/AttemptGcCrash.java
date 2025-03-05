// https://stackoverflow.com/a/74780/13458117
public class AttemptGcCrash {
    public static void main(String[] args) {
        try {
            Object[] o = null;

            while (true) {
                o = new Object[] {o};
            }
        } catch (OutOfMemoryError error) {
            System.out.println("Out of memory!");
        }
    }
}