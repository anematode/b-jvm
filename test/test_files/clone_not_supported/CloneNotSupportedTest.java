
class CloneNotSupported {
    public Object clone() throws CloneNotSupportedException {
        throw new CloneNotSupportedException();
    }
}

class VeryMuchSupported implements Cloneable {
    public Object clone() throws CloneNotSupportedException {
        return super.clone();
    }
}

public class CloneNotSupportedTest {
    public static void main(String[] args) {
        CloneNotSupported cloneNotSupported = new CloneNotSupported();
        try {
            cloneNotSupported.clone();
        } catch (CloneNotSupportedException e) {
            System.out.println("CloneNotSupportedException caught");
        }

        VeryMuchSupported veryMuchSupported = new VeryMuchSupported();
        try {
            veryMuchSupported.clone();
        } catch (CloneNotSupportedException e) {
            System.out.println("This is bad!");
        }

        veryMuchSupported = null;
        try {
            veryMuchSupported.clone();
        } catch (CloneNotSupportedException e) {
            System.out.println("This is bad!");
        } catch (NullPointerException e) {
            System.out.println("NullPointerException caught");
        }
    }
}