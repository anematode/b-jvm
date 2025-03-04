type JavaType = {
	baseType:
		| "double"
		| "int"
		| "boolean"
		| "float"
		| "long"
		| "short"
		| "byte"
		| "char"
		| "void"
		| "object";
	arrayDimensions: number;
	objectName: string;
	typeParameters: JavaType[];
};

type MethodInfo = {
	name: string;
	returnType: JavaType;
	parameters: { name: string; type: JavaType }[];
	typeParameters: string[];
};

type FieldInfo = {
	name: string;
	type: JavaType;
};

type ClassInfo = {
	name: string;
	methods: MethodInfo[];
	fields: FieldInfo[];
};

function parseJavaNonArrayType(type: string): JavaType {
	switch (type) {
		case "double":
		case "int":
		case "boolean":
		case "float":
		case "long":
		case "short":
		case "byte":
		case "char":
		case "void":
			return {
				baseType: type,
				arrayDimensions: 0,
				objectName: "",
				typeParameters: [],
			};
		default:
			// Look for type parameters
			const typeParameters: JavaType[] = [];
			let i = type.indexOf("<");
			if (i === -1) {
				return {
					baseType: "object",
					arrayDimensions: 0,
					objectName: type,
					typeParameters,
				};
			}
			const objectName = type.substring(0, i);
			i++;
			let depth = 1;
			let start = i;
			while (depth > 0) {
				if (type[i] === "<") {
					depth++;
				} else if (type[i] === ">") {
					depth--;
				}
				if (depth === 0) {
					typeParameters.push(
						parseJavaType(type.substring(start, i))
					);
				}
				i++;
			}
			return {
				baseType: "object",
				arrayDimensions: 0,
				objectName,
				typeParameters,
			};
	}
}

function parseJavaType(type: string): JavaType {
	let i = 0;
	while (type[i] === "[") {
		i++;
	}
	const arrayDimensions = i;
	const nonArray = parseJavaNonArrayType(type.substring(i));
	nonArray.arrayDimensions = arrayDimensions;
	return nonArray;
}

export const parseJavap = (output: string): ClassInfo => {
	// Compiled from "Egg.java"
	// class Example<B> {
	//   public int x;
	//   public java.util.ArrayList<java.lang.Double> y;
	//   public java.lang.Object z;
	//   public java.util.ArrayList<java.lang.String> returnsArrayListString();
	//   public B returnsTypeParameter();
	//   public void egg(double);
	//   public void egg(int);
	// }

	const lines = output.split("\n");
	const className = lines[1]!;
	// Look for contents between 'class' and '{'
	const regex = /class ([^<]+)(<([^>]+)>)? {/;
	const match = className.match(regex);
	if (!match) {
		throw new Error("Invalid class name: " + className);
	}

	const name = match[1]!;
	const typeParameters = match[3] ? match[3].split(",") : [];

	const methods: MethodInfo[] = [];
	const fields: FieldInfo[] = [];

	for (let i = 2; i < lines.length; i++) {
		const trimmed = lines[i]!.trim();
		if (trimmed.startsWith("public ")) {
			const line = trimmed.substring("public ".length);
			if (line.includes("(")) {
				// Method
				const returnType = parseJavaType(
					line.substring(0, line.indexOf(" ")).trim()
				);
				const name = line
					.substring(line.indexOf(" ") + 1, line.indexOf("("))
					.trim();
				let parameters: { name: string; type: JavaType }[] = [];
				if (line.indexOf("(") !== -1) {
					parameters = line
						.substring(line.indexOf("(") + 1, line.indexOf(")"))
						.split(", ")
						.map((param, i) => {
							const split = param.split(" ");
							return {
								name: split[1] ?? "arg" + i,
								type: parseJavaType(split[0]!),
							};
						});
				}

				methods.push({ name, returnType, parameters, typeParameters });
			} else {
				// Field
				const type = line.substring(0, line.indexOf(" ")).trim();
				const name = line
					.substring(line.indexOf(" ") + 1, line.length - 1)
					.trim();
				fields.push({ name, type: parseJavaType(type) });
			}
		}
	}

	return { name, methods, fields };
};
