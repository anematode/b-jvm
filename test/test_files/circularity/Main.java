class Egg extends Chicken {

}

class Chick extends Egg {

}

class Chicken extends Chick {

}

public class Main {
    public static void main(String[] args) {
        try {
            Chick chick = new Chick();
        } catch (ClassCircularityError e) {
            System.out.print("a");
        }
        try {
            Egg egg = new Egg();
        } catch (ClassCircularityError e) {
            System.out.print("b");
        }
        try {
            Chicken chicken = new Chicken();
        } catch (ClassCircularityError e) {
            System.out.print("c");
        }
    }
}
