public class GenericBox<T> {
    private T t;

    public void set(T t) {
        this.t = t;
    }

    public T get() {
        return t;
    }

    public static void main(String[] args) {
        GenericBox<Integer> integerBox = new GenericBox<>();
        GenericBox<String> stringBox = new GenericBox<>();

        integerBox.set(10);
        stringBox.set("Hello World");

        System.out.println("Integer Value: " + integerBox.get());
        System.out.println("String Value: " + stringBox.get());
        // System.out.printf("Integer Value: %d\n", integerBox.get());
        // System.out.printf("String Value: %s\n", stringBox.get());

        GenericBox<GenericBox<Integer>> boxInBox = new GenericBox<>();
        boxInBox.set(integerBox);
        System.out.println("Box in Box Value: " + boxInBox.get().get());
        // System.out.printf("Box in Box Value: %d\n", boxInBox.get().get());

        System.out.println("Cursed time");
        boxInBox.set((GenericBox<Integer>) ((GenericBox) stringBox)); // should technically work due to type erasure
        GenericBox<Integer> falseBox = boxInBox.get();
        System.out.println("Reached");

        // should fail
        try {
            int value = falseBox.get();
            System.out.println("This should be unreachable!");
            System.out.println("Box in Box Value: " + value);
            // System.out.printf("Box in Box Value: %d\n", value);
        } catch (ClassCastException e) {
            System.out.println("Correctly threw ClassCastException upon getting");
        }
        System.out.println("Done");
    }
}