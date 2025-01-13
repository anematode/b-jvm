package bjvm.misc;

import java.lang.classfile.ClassFile;
import java.lang.classfile.CodeBuilder;
import java.lang.classfile.CodeElement;
import java.lang.classfile.instruction.FieldInstruction;
import java.lang.constant.ConstantDescs;
import java.lang.constant.DynamicCallSiteDesc;
import java.lang.constant.MethodTypeDesc;

import static bjvm.misc.AccessorMetafactory.GETTER_FACTORY;
import static bjvm.misc.AccessorMetafactory.SETTER_FACTORY;
import static java.lang.classfile.ClassTransform.transformingMethodBodies;
import static java.lang.classfile.Opcode.GETFIELD;
import static java.lang.classfile.Opcode.PUTFIELD;

final class MemberRewriter {
	private static final ClassFile CF = ClassFile.of();

	public static byte[] rewrite(byte[] klass) {
		var model = CF.parse(klass);
		var result = CF.transform(model, transformingMethodBodies(MemberRewriter::transformCodeElement));
		return result;
	}

	private static void transformCodeElement(CodeBuilder builder, CodeElement element) {
		switch (element) {
			case FieldInstruction instruction when instruction.opcode() == GETFIELD -> handleGetfield(builder, instruction);
			case FieldInstruction instruction when instruction.opcode() == PUTFIELD -> handlePutfield(builder, instruction);
			default -> builder.with(element);
		}
	}

	private static void handleGetfield(CodeBuilder builder, FieldInstruction instruction) {
		assert instruction.opcode() == GETFIELD;

		var className = instruction.owner().name().stringValue().replace('/', '.');
		var fieldName = instruction.field().name().stringValue();

		builder.invokedynamic(DynamicCallSiteDesc.of(
				GETTER_FACTORY,
				"getter$$" + fieldName,
				MethodTypeDesc.of(instruction.typeSymbol(), instruction.owner().asSymbol()),
				className, fieldName
		));

	}

	private static void handlePutfield(CodeBuilder builder, FieldInstruction instruction) {
		assert instruction.opcode() == PUTFIELD;

		var className = instruction.owner().name().stringValue().replace('/', '.');
		var fieldName = instruction.field().name().stringValue();

		builder.invokedynamic(DynamicCallSiteDesc.of(
				SETTER_FACTORY,
				"setter$$" + fieldName,
				MethodTypeDesc.of(ConstantDescs.CD_void, instruction.owner().asSymbol(), instruction.typeSymbol()),
				className, fieldName
		));
	}
}
