import java.util.ArrayList;

public class ExternalClass {
    public ExternalClass() {
        ArrayList<Integer> list = new ArrayList<>();
        list.add(1);
        list.forEach(ExternalClass::enjoyment);
    }

    static void enjoyment(int a) {
        System.out.println("ExternalClass instance created!");
    }

    public void someMethod() {
        System.out.println("someMethod() in ExternalClass was called!");
    }
}