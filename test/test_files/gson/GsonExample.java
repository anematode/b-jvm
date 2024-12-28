import com.google.gson.Gson;

class Student {
    private String name;
    private int age;

    public Student() {
        this.name = "";
        this.age = 0;
    }

    public Student(String name, int age) {
        this.name = name;
        this.age = age;
    }

    public String toString() {
        return "Student: " + name + " is " + age + " years old.";
    }
}

public class GsonExample {
    public static void main(String[] args) {
        Gson gson = new Gson();
        String json = "{\"name\":\"Goober\", \"age\":21}";
        Student student = gson.fromJson(json, Student.class);
        System.out.println(student);

        // And then serialize it back to JSON
        String json2 = gson.toJson(student);
        System.out.println(json2);
    }
}