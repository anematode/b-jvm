import { ClassInfo, JavaType } from "./parseJavap";

const uppercaseFirst = (str: string) =>
	str.charAt(0).toUpperCase() + str.slice(1);

const encodeJavaClassName = (name: string) => {
	return name.replace(/_/g, "__").replace(/\./g, "_");
};

export const javaTypeToTsType = (javaType: JavaType): string => {
	if (javaType.kind === "primitive") {
		const typeMap = {
			boolean: "boolean",
			byte: "number", // ?? what should this be?
			char: "number", // ?? what should this be?
			double: "number",
			float: "number",
			int: "number",
			long: "number",
			short: "number",
			void: "void",
		};
		return typeMap[javaType.type];
	} else if (javaType.kind === "array") {
		if (javaType.type.kind === "primitive") {
			return `Java${uppercaseFirst(javaType.type.type)}Array<${
				javaType.dimensions
			}>`;
		} else {
			return `JavaArray<${javaTypeToTsType(javaType.type)}, ${
				javaType.dimensions
			}>`;
		}
	} else if (javaType.kind === "class") {
		// Special case for java.lang.String -> string
		if (javaType.name === "java.lang.String") {
			return "string";
		}
		return `${encodeJavaClassName(javaType.name)}`;
	} else {
		throw new Error(`Unknown java type: ${JSON.stringify(javaType)}`);
	}
};

export const genTsTypeFromClassInfo = (classInfo: ClassInfo): string => {
	const fields = classInfo.fields.map(
		(fieldInfo) => `${fieldInfo.name}: ${javaTypeToTsType(fieldInfo.type)}`
	);
	const methods = classInfo.methods
		.filter((methodInfo) => methodInfo.kind !== "constructor")
		.map(
			(methodInfo) =>
				`${methodInfo.name}(${methodInfo.parameters
					.map(
						(p, i) =>
							`${p.name ?? `arg_${i}`}: ${javaTypeToTsType(
								p.type
							)}`
					)
					.join(", ")}): ${javaTypeToTsType(methodInfo.returnType)}`
		);
	const constructors = classInfo.methods
		.filter((methodInfo) => methodInfo.kind === "constructor")
		.map(
			(methodInfo) =>
				`new(${methodInfo.parameters
					.map(
						(p, i) =>
							`${p.name ?? `arg_${i}`}: ${javaTypeToTsType(
								p.type
							)}`
					)
					.join(", ")}): ${javaTypeToTsType(methodInfo.returnType)}`
		);

	const implementsClause =
		` implements ` +
		classInfo.interfaces.map((i) => encodeJavaClassName(i)).join(", ");

	const type = `declare ${
		classInfo.isInterface ? "interface" : "class"
	} ${encodeJavaClassName(
		`${classInfo.packageName}.${classInfo.className}`
	)}${
		classInfo.superClass
			? ` extends ${encodeJavaClassName(classInfo.superClass)}`
			: ""
	}${classInfo.interfaces.length ? implementsClause : ""} {
	${[...constructors, ...fields, ...methods].join(";\n\t")}
	[_BRAND]: {
		${[classInfo.className, ...classInfo.interfaces]
			.map(
				(interfaceName) => `${encodeJavaClassName(interfaceName)}: true`
			)
			.join(";\n\t\t")}
	};
}`;
	return type;
};
