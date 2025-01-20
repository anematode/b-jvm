
sealed interface Permits permits Subclass1, Subclass2, Subclass3 {
}

final class Subclass1 implements Permits {
}

final class Subclass2 implements Permits {
}

final class Subclass3 implements Permits {
}

public class PermittedSubclasses {
    public static void print(Permits p) {
        switch (p) {
            case Subclass1 s1 -> System.out.println("Subclass1");
            case Subclass2 s2 -> System.out.println("Subclass2");
            case Subclass3 s3 -> System.out.println("Subclass3");
        }
    }

    public static void main(String[] args) {
        System.out.println("PermittedSubclasses");

        Permits a = new Subclass1();
        Permits b = new Subclass2();
        Permits c = new Subclass3();

        print(a);
        print(b);
        print(c);

        for (Class<?> permittedSubclass : Permits.class.getPermittedSubclasses()) {
            System.out.println(permittedSubclass.getName());
        }
    }
}