// Taken from https://counterexamples.org/general-covariance.html
class Vehicle {}
class Car extends Vehicle {}
class Bus extends Vehicle {}
public class ArrayStore {
  public static void main(String[] args) {
    Car[] c = { new Car() };
    Vehicle[] v = c;
    try {
        v[0] = new Bus();
    } catch (ArrayStoreException e) {
        e.printStackTrace();
    }
  }
}