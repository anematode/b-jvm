import { ClassInfo, JavaType } from "./parseJavap";

export const javaTypeToTsType = (javaType: JavaType): string => {
	if (javaType.kind === "primitive") {
		return `JavaPrimitive<"${javaType.type}">`;
	} else if (javaType.kind === "array") {
		return `JavaArray<${javaTypeToTsType(javaType.type)}>`;
	} else if (javaType.kind === "class") {
		return `JavaClass<"${javaType.name}">`;
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

	const type = `declare class ${classInfo.className}${
		classInfo.superClass
			? ` extends JavaClass<"${classInfo.superClass}">`
			: ""
	} implements ${classInfo.interfaces
		.map((i) => `JavaClass<"${i}">`)
		.join(", ")} {
	${[...constructors, ...fields, ...methods].join(";\n\t")}
	[_BRAND]: {
		${[classInfo.className, ...classInfo.interfaces]
			.map((interfaceName) => `"${interfaceName}": true`)
			.join(";\n\t\t")}
	};
}`;
	return type;
};
