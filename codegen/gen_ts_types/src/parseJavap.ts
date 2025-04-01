export type JavaPrimitiveType =
	| "double"
	| "int"
	| "boolean"
	| "float"
	| "long"
	| "short"
	| "byte"
	| "char"
	| "void";

export type JavaType =
	| {
			kind: "primitive";
			type: JavaPrimitiveType;
	  }
	| {
			kind: "class";
			name: string;
	  }
	| {
			kind: "array";
			type: JavaType;
			dimensions: number;
	  };

export type JavaModifier =
	| "public"
	| "private"
	| "protected"
	| "static"
	| "final"
	| "abstract"
	| "synchronized"
	| "volatile"
	| "transient";

/**
 * Represents a parameter in a Java method
 */
export type JavaMethodParameter = {
	/** The type of the parameter */
	type: JavaType;
	/** The name of the parameter, if available */
	name?: string;
	/** Indicates if this parameter is a varargs parameter (e.g., String... args) */
	isVarargs?: boolean;
};

/**
 * Represents a type parameter in a generic class or method
 * e.g., 'T', 'E extends Exception', 'K extends Comparable<K>'
 */
export type TypeParameter = {
	/** The name of the type parameter (e.g., 'T', 'E', 'K') */
	name: string;
	/** The bounds of the type parameter, if any (e.g., 'Comparable<T>', 'Exception') */
	bounds?: string[];
};

export type JavaMethodKind = "constructor" | "method";

export type JavaMethod = {
	name: string;
	kind: JavaMethodKind;
	modifiers: JavaModifier[];
	returnType: JavaType;
	parameters: JavaMethodParameter[];
	descriptor?: string;
	throws?: string[];
	/** Type parameters for generic methods, e.g., '<T>' or '<K, V extends Comparable<V>>' */
	typeParameters?: TypeParameter[];
};

export type JavaField = {
	name: string;
	modifiers: JavaModifier[];
	type: JavaType;
	descriptor?: string;
};

export type ClassInfo = {
	className: string;
	packageName?: string;
	modifiers: JavaModifier[];
	superClass?: string;
	interfaces: string[];
	methods: JavaMethod[];
	fields: JavaField[];
	isInterface?: boolean;
	/** Type parameters for generic classes, e.g., '<T>' or '<K, V extends Comparable<V>>' */
	typeParameters?: TypeParameter[];
};

/**
 * Parse a Java type string into a structured JavaType object
 * Enhanced to handle complex nested generics
 */
function parseJavaType(typeStr: string): JavaType {
	// Handle generic types
	if (typeStr.includes("<")) {
		// Extract the base type and generic arguments
		const baseType = typeStr.substring(0, typeStr.indexOf("<"));
		const genericPart = extractGenericPart(typeStr);

		// Process the extracted generic part if successful
		if (genericPart) {
			// For simplicity, we just store the full generic type as a class type
			// A more advanced implementation could parse the generic arguments into a structured format
			return {
				kind: "class",
				name: `${baseType}${genericPart}`,
			};
		}
	}

	// Handle array types
	const arrayMatch = typeStr.match(/^(.+?)(\[\])+$/);
	if (arrayMatch) {
		const [, baseType, arrayBrackets] = arrayMatch;
		return {
			kind: "array",
			type: parseJavaType(baseType),
			dimensions: arrayBrackets.length / 2,
		};
	}

	// Handle primitive types
	const primitiveTypes: JavaPrimitiveType[] = [
		"double",
		"int",
		"boolean",
		"float",
		"long",
		"short",
		"byte",
		"char",
		"void",
	];

	if (primitiveTypes.includes(typeStr as JavaPrimitiveType)) {
		return {
			kind: "primitive",
			type: typeStr as JavaPrimitiveType,
		};
	}

	// Everything else is a class type
	return {
		kind: "class",
		name: typeStr,
	};
}

/**
 * Helper function to extract the generic part of a type, handling nested angle brackets
 */
function extractGenericPart(typeStr: string): string | null {
	let nestLevel = 0;
	let result = "";
	let started = false;

	for (let i = 0; i < typeStr.length; i++) {
		const char = typeStr[i];

		if (char === "<") {
			nestLevel++;
			result += char;
			started = true;
		} else if (char === ">") {
			nestLevel--;
			result += char;

			if (nestLevel === 0 && started) {
				return result;
			}
		} else if (started) {
			result += char;
		}
	}

	return null; // Return null if the generic part couldn't be properly extracted
}

function parseParameters(paramsStr: string): JavaMethodParameter[] {
	if (!paramsStr.trim()) return [];

	// Handle generic types with commas inside them
	// We need to parse parameters more carefully to handle generics
	const params: JavaMethodParameter[] = [];
	let currentParam = "";
	let angleBracketDepth = 0;

	// Split parameters respecting generic type boundaries
	for (let i = 0; i < paramsStr.length; i++) {
		const char = paramsStr[i];

		if (char === "<") {
			angleBracketDepth++;
			currentParam += char;
		} else if (char === ">") {
			angleBracketDepth--;
			currentParam += char;
		} else if (char === "," && angleBracketDepth === 0) {
			// Only split on commas outside of generic types
			if (currentParam.trim()) {
				params.push(parseParameter(currentParam.trim()));
			}
			currentParam = "";
		} else {
			currentParam += char;
		}
	}

	// Add the last parameter
	if (currentParam.trim()) {
		params.push(parseParameter(currentParam.trim()));
	}

	return params;
}

function parseParameter(paramStr: string): JavaMethodParameter {
	// Check for varargs notation
	const isVarargs = paramStr.includes("...");

	// Remove the varargs ellipsis for further processing
	if (isVarargs) {
		paramStr = paramStr.replace("...", "");
	}

	// For complex generic types, we need to find the last space that's outside generic brackets
	// This separates the type from the parameter name
	let lastOutsideSpace = -1;
	let angleBracketDepth = 0;

	for (let i = 0; i < paramStr.length; i++) {
		const char = paramStr[i];

		if (char === "<") {
			angleBracketDepth++;
		} else if (char === ">") {
			angleBracketDepth--;
		} else if (char === " " && angleBracketDepth === 0) {
			lastOutsideSpace = i;
		}
	}

	// If we found a space outside generic brackets
	if (lastOutsideSpace !== -1) {
		const typeStr = paramStr.substring(0, lastOutsideSpace).trim();
		const nameStr = paramStr.substring(lastOutsideSpace + 1).trim();

		// Parse the type first
		let type = parseJavaType(typeStr);

		// For varargs, we need to convert the type to an array type if it's not already an array
		if (isVarargs && type.kind !== "array") {
			type = {
				kind: "array",
				type,
				dimensions: 1,
			};
		}

		// Create the parameter object with base properties
		const param: JavaMethodParameter = {
			type,
			name: nameStr,
		};

		// Only add isVarargs property if it's true
		if (isVarargs) {
			param.isVarargs = true;
		}

		return param;
	}

	// No space found, the whole string is the type
	let type = parseJavaType(paramStr.trim());

	// For varargs, we need to convert the type to an array type if it's not already an array
	if (isVarargs && type.kind !== "array") {
		type = {
			kind: "array",
			type,
			dimensions: 1,
		};
	}

	// Create the parameter object with base properties
	const param: JavaMethodParameter = {
		type,
	};

	// Only add isVarargs property if it's true
	if (isVarargs) {
		param.isVarargs = true;
	}

	return param;
}

/**
 * Helper function to parse interfaces from an implements clause
 */
function parseCommaSeparatedList(str: string): string[] {
	if (!str) return [];

	// Clean up the string and split by commas
	return str
		.trim()
		.split(/\s*,\s*/)
		.map((intf) => intf.trim())
		.filter(Boolean);
}

/**
 * Parse type parameters from a string like '<T>', '<K, V>', or '<E extends Exception>'
 * @param typeParamsStr The string containing type parameters, including angle brackets
 * @returns Array of TypeParameter objects
 */
function parseTypeParameters(typeParamsStr: string): TypeParameter[] {
	if (!typeParamsStr || typeParamsStr.length < 3) return []; // Need at least '<T>'

	// Strip the outer angle brackets
	const content = typeParamsStr.substring(1, typeParamsStr.length - 1);

	const typeParams: TypeParameter[] = [];
	let currentParam = "";
	let angleBracketDepth = 0;

	// Parse each type parameter, being careful with nested angle brackets
	for (let i = 0; i < content.length; i++) {
		const char = content[i];

		if (char === "<") {
			angleBracketDepth++;
			currentParam += char;
		} else if (char === ">") {
			angleBracketDepth--;
			currentParam += char;
		} else if (char === "," && angleBracketDepth === 0) {
			// Only split on commas outside of nested angle brackets
			if (currentParam.trim()) {
				typeParams.push(parseTypeParameter(currentParam.trim()));
			}
			currentParam = "";
		} else {
			currentParam += char;
		}
	}

	// Don't forget the last parameter
	if (currentParam.trim()) {
		typeParams.push(parseTypeParameter(currentParam.trim()));
	}

	return typeParams;
}

/**
 * Parse a single type parameter like 'T' or 'E extends Exception'
 * @param paramStr String representation of the type parameter
 * @returns TypeParameter object
 */
function parseTypeParameter(paramStr: string): TypeParameter {
	const parts = paramStr.split(/\s+extends\s+/);
	const name = parts[0].trim();

	if (parts.length > 1) {
		// Has bounds (e.g., 'E extends Exception' or 'K extends Comparable<K>')
		const boundsStr = parts.slice(1).join(" extends ");
		const bounds = boundsStr.split(/\s*&\s*/); // Handle multiple bounds with &
		return { name, bounds };
	}

	return { name };
}

/**
 * Parses a string of javap output and returns a structured representation
 */
export function parseJavap(javapOutputString: string): ClassInfo[] {
	const lines = javapOutputString.split("\n").map((l) => l.trim());
	const makeNewResult = (): ClassInfo => ({
		className: "",
		modifiers: [],
		interfaces: [],
		methods: [],
		fields: [],
		isInterface: false,
	});
	let result: ClassInfo = null as never;
	const results: ClassInfo[] = [];

	for (const line of lines) {
		if (line === "") {
			continue;
		}
		if (line.startsWith("Compiled from")) {
			result = makeNewResult();
			results.push(result);
			continue;
		}

		// Handle interface declaration
		const interfaceMatch = line.match(
			/^((?:public |private |protected |abstract )*)?interface\s+([^\s{]+)(?:\s+extends\s+([^{]+))?\s*\{?/
		);
		if (interfaceMatch) {
			const [, modifiersStr, fullInterfaceName, extendedInterfaces] =
				interfaceMatch;

			// Handle package and interface name
			const lastDotIndex = fullInterfaceName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				result.packageName = fullInterfaceName.substring(
					0,
					lastDotIndex
				);
				result.className = fullInterfaceName.substring(
					lastDotIndex + 1
				);
			} else {
				result.className = fullInterfaceName;
			}

			// Set isInterface flag
			result.isInterface = true;

			// Handle modifiers
			if (modifiersStr) {
				result.modifiers = modifiersStr
					.trim()
					.split(" ")
					.filter(Boolean) as JavaModifier[];
			}

			// Handle extended interfaces
			if (extendedInterfaces) {
				result.interfaces = parseCommaSeparatedList(extendedInterfaces);
			}
			continue;
		}

		// Handle class declaration - check this after interface to avoid mismatches
		if (line.match(/\bclass\b/)) {
			// First, try to extract the basic parts of the class declaration
			// This improved regex captures the generic type parameters separately
			const classDeclarationParts = line.match(
				/^((?:public |private |protected |final |abstract )*)?class\s+([^\s<{]+)(?:(<[^>]+>))?(.*)$/
			);

			if (classDeclarationParts) {
				const [
					,
					modifiersStr,
					fullClassName,
					typeParamsStr,
					remainingDeclaration,
				] = classDeclarationParts;

				// Handle package and class name
				const lastDotIndex = fullClassName.lastIndexOf(".");
				if (lastDotIndex !== -1) {
					result.packageName = fullClassName.substring(
						0,
						lastDotIndex
					);
					result.className = fullClassName.substring(
						lastDotIndex + 1
					);
				} else {
					result.className = fullClassName;
				}

				// Handle modifiers
				if (modifiersStr) {
					result.modifiers = modifiersStr
						.trim()
						.split(" ")
						.filter(Boolean) as JavaModifier[];
				}

				// Parse type parameters if present
				if (typeParamsStr) {
					result.typeParameters = parseTypeParameters(typeParamsStr);
				}

				// Now, separately handle extends and implements clauses
				const extendsMatch = remainingDeclaration.match(
					/\s+extends\s+([^\s{]+(?:<[^>]+>)?)/
				);
				if (extendsMatch) {
					result.superClass = extendsMatch[1].trim();
				}

				const implementsMatch = remainingDeclaration.match(
					/\s+implements\s+([^{]+)/
				);
				if (implementsMatch) {
					const implementsStr = implementsMatch[1].trim();
					result.interfaces = parseCommaSeparatedList(implementsStr);
				}
			}
			continue;
		}

		// Exact constructor pattern - matches the specific format like "public com.example.ClassName();"
		const exactConstructorMatch = line.match(
			/^\s*((?:public |private |protected )*)([^(.\s]+(?:\.[^(.\s]+)+)\(\);(?:\s*\/\/\s*(.+))?$/
		);

		// More specific constructor pattern - for constructors with parameters
		const constructorWithParamsMatch = line.match(
			/^\s*((?:public |private |protected )*)([^(.\s]+(?:\.[^(.\s]+)+)\(([^)]*)\);(?:\s*\/\/\s*(.+))?$/
		);

		if (exactConstructorMatch) {
			const [, modifiersStr, fullConstructor, descriptor] =
				exactConstructorMatch;

			// Extract the constructor name and check if it matches the class name
			const constructorAndParams = fullConstructor.split("(")[0];
			const lastDotIndex = constructorAndParams.lastIndexOf(".");

			if (lastDotIndex !== -1) {
				const simpleName = constructorAndParams.substring(
					lastDotIndex + 1
				);

				if (simpleName === result.className) {
					const method: JavaMethod = {
						name: simpleName,
						kind: "constructor",
						returnType: {
							kind: "class",
							name: constructorAndParams, // Full qualified name
						},
						modifiers: modifiersStr
							? (modifiersStr
									.trim()
									.split(" ")
									.filter(Boolean) as JavaModifier[])
							: [],
						parameters: [],
						...(descriptor ? { descriptor } : {}),
					};

					result.methods.push(method);
					continue;
				}
			}
		}

		if (constructorWithParamsMatch && !exactConstructorMatch) {
			const [, modifiersStr, fullConstructor, paramsStr, descriptor] =
				constructorWithParamsMatch;

			// Extract the constructor name and check if it matches the class name
			const constructorName = fullConstructor.split("(")[0];
			const lastDotIndex = constructorName.lastIndexOf(".");

			if (lastDotIndex !== -1) {
				const simpleName = constructorName.substring(lastDotIndex + 1);

				if (simpleName === result.className) {
					const method: JavaMethod = {
						name: simpleName,
						kind: "constructor",
						returnType: {
							kind: "class",
							name: constructorName, // Full qualified name
						},
						modifiers: modifiersStr
							? (modifiersStr
									.trim()
									.split(" ")
									.filter(Boolean) as JavaModifier[])
							: [],
						parameters: parseParameters(paramsStr),
						...(descriptor ? { descriptor } : {}),
					};

					result.methods.push(method);
					continue;
				}
			}
		}

		// Special case for methods with complex nested generic return types
		const complexGenericMethodMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(java\.util\.[^<]+<.+>)\s+(\S+)\(([^)]*)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
		);

		// Special case for generic methods with type parameters and complex return types
		const genericWithTypeParamsMethodMatch =
			!complexGenericMethodMatch &&
			line.match(
				/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)<([^>]+)>\s+([^<\s]+(?:<.+>)?)\s+(\S+)\(([^)]*)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
			);

		if (complexGenericMethodMatch) {
			// Handle method with complex nested generic return type
			const [
				,
				modifiersStr,
				returnType,
				name,
				paramsStr,
				throwsStr,
				descriptor,
			] = complexGenericMethodMatch;

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				...(descriptor ? { descriptor } : {}),
			};

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		} else if (genericWithTypeParamsMethodMatch) {
			// Handle generic method with type parameters
			const [
				,
				modifiersStr,
				typeParamsStr,
				returnType,
				name,
				paramsStr,
				throwsStr,
				descriptor,
			] = genericWithTypeParamsMethodMatch;

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				...(descriptor ? { descriptor } : {}),
			};

			// Parse type parameters
			if (typeParamsStr) {
				method.typeParameters = parseTypeParameters(
					`<${typeParamsStr}>`
				);
			}

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		}

		// First, try with complex generic type parameters with bounds
		const methodComplexGenericMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(?:(<.*?extends.*?>)\s+)(\S+(?:<[^>]+>)?)\s+(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
		);

		// Then try simpler generic methods
		const methodGenericMatch =
			!methodComplexGenericMatch &&
			line.match(
				/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(?:(<.+?>)\s+)(\S+)\s+(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
			);

		// And regular method declarations
		const methodMatch =
			!methodComplexGenericMatch &&
			!methodGenericMatch &&
			line.match(
				/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(\S+(?:<[^>]+>)?)\s+(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
			);

		if (methodComplexGenericMatch) {
			// Handle complex generic method declaration
			const [
				,
				modifiersStr,
				typeParamsStr,
				returnType,
				fullName,
				paramsStr,
				throwsStr,
				descriptor,
			] = methodComplexGenericMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				...(descriptor ? { descriptor } : {}),
			};

			// Parse type parameters
			if (typeParamsStr) {
				method.typeParameters = parseTypeParameters(typeParamsStr);
			}

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		} else if (methodGenericMatch) {
			// Handle generic method declaration
			const [
				,
				modifiersStr,
				typeParamsStr,
				returnType,
				fullName,
				paramsStr,
				throwsStr,
				descriptor,
			] = methodGenericMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				...(descriptor ? { descriptor } : {}),
			};

			// Parse type parameters
			if (typeParamsStr) {
				method.typeParameters = parseTypeParameters(typeParamsStr);
			}

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		} else if (methodMatch) {
			// Handle regular method declaration
			const [
				,
				modifiersStr,
				returnType,
				fullName,
				paramsStr,
				throwsStr,
				descriptor,
			] = methodMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				...(descriptor ? { descriptor } : {}),
			};

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		}

		// Parse fields - improved regex to handle both interface constants and class fields
		// This needs to come after method parsing to avoid mismatches with method declarations
		// First, try a more specific regex for fields with complex generic types
		const complexFieldMatch = line.match(
			/^\s*((?:public |private |protected |static |final |volatile |transient )*)?(\S+(?:<.+>))\s+(\S+)(?:\s*=\s*[^;]*)?;(?:\s*\/\/\s*(.+))?$/
		);

		// Then try the simpler regex for regular fields
		const fieldMatch =
			!complexFieldMatch &&
			line.match(
				/^\s*((?:public |private |protected |static |final |volatile |transient )*)?(\S+)\s+(\S+)(?:\s*=\s*[^;]*)?;(?:\s*\/\/\s*(.+))?$/
			);

		// Ensure we don't match methods that were missed by the method regexes
		const isLikelyMethod =
			(complexFieldMatch || fieldMatch) &&
			(complexFieldMatch
				? complexFieldMatch[3]
				: fieldMatch![3]
			).includes("(");

		if ((complexFieldMatch || fieldMatch) && !isLikelyMethod) {
			const [, modifiersStr, type, name, descriptor] =
				complexFieldMatch || fieldMatch!;
			const field: JavaField = {
				name,
				type: parseJavaType(type),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				...(descriptor ? { descriptor } : {}),
			};
			result.fields.push(field);
			continue;
		}
	}

	return results;
}
