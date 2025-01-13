import com.itextpdf.kernel.pdf.PdfWriter;
import com.itextpdf.kernel.pdf.PdfDocument;
import com.itextpdf.layout.Document;
import com.itextpdf.layout.element.Paragraph;

public class PDFDemo {
    public static void main(String[] args) {
        // Path where the PDF will be created
        String pdfPath = "example.pdf";

        try {
            // Initialize PDF writer
            PdfWriter writer = new PdfWriter(pdfPath);

            // Initialize PDF document
            PdfDocument pdf = new PdfDocument(writer);

            // Initialize document layout
            Document document = new Document(pdf);

            // Add content to the PDF
            document.add(new Paragraph("Hello, iTextPDF!"));
            document.add(new Paragraph("This is a simple PDF created using iTextPDF."));

            // Close the document
            document.close();

            System.out.println("PDF created successfully at: " + pdfPath);
        } catch (Exception e) {
            e.printStackTrace();
        }
    }
}