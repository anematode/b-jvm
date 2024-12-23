
class Eggnog {
    static {
        System.out.println("This should not be printed!");
    }
}

public class Main {
    public static void main(String[] args) {
        Eggnog[] a = new Eggnog[1];
        System.out.println("Hey :)");
    }
}