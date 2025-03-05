

public class StackOverflow {
    static int count = 0;
    public static void main(String[] args) {
        try {
            StackOverflow stackOverflow = new StackOverflow();
            stackOverflow.stackOverflow();
        } catch (StackOverflowError e) {
            System.out.println("StackOverflowError caught");
            System.out.println("Count: " + count);
        }
    }

    public void stackOverflow() {
        count++;
        stackOverflow0();
    }

    public void stackOverflow0() {
        count++;
        stackOverflow();
    }
}