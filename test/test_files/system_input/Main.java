
public class Main {
    public static void main(String[] args) throws Exception {
        System.out.println("Write a byte");

        int res = System.in.read();
        if (res == -1) {
            System.out.println("EOF");
        } else {
            System.out.println("Data read: " + res);
            System.out.println("As a char: " + (char) res);
        }
    }
}