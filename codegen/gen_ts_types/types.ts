// --- Types ---

type JavaPrimitive<T> = T;
type JavaClass<T> = T;
type JavaArray<T> = T[];

declare class GameOfLife {
	new(): JavaClass<"GameOfLife">;
	new(arg_0: JavaPrimitive<"int">, arg_1: JavaPrimitive<"int">): GameOfLife;
	iterate(): JavaPrimitive<"void">;
	randomSeed(): JavaPrimitive<"void">;
	getData(): JavaArray<JavaPrimitive<"byte">>;
}

declare class Conway2D {
	new(): JavaClass<"Conway2D">;
	game: JavaClass<"GameOfLife">;
	main(
		arg_0: JavaArray<JavaClass<"java.lang.String">>
	): JavaPrimitive<"void">;
	enjoyment(): JavaClass<"java.lang.String">;
	step(): JavaClass<"java.lang.String">;
}
