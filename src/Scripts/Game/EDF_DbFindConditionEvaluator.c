enum EDF_DbFindFieldPathSegmentFlags
{
	NUMBER = 1,
	TYPENAME = 2,
	ANY = 4,
	ALL = 8,
	COUNT = 16,
	LENGTH = 32,
	KEYS = 64,
	VALUES = 128
};

class EDF_DbFindFieldPathSegment
{
	string m_sFieldName;
	int m_iFlags;

	//------------------------------------------------------------------------------------------------
	void EDF_DbFindFieldPathSegment(string fieldName, int flags)
	{
		m_sFieldName = fieldName;
		m_iFlags = flags;
	}
};

class EDF_DbFindConditionEvaluator
{
	//------------------------------------------------------------------------------------------------
	static array<ref EDF_DbEntity> GetFiltered(notnull array<ref EDF_DbEntity> entities, notnull EDF_DbFindCondition condition)
	{
		array<ref EDF_DbEntity> conditionMatched();

		foreach (EDF_DbEntity entity : entities)
		{
			if (EDF_DbFindConditionEvaluator.Evaluate(entity, condition))
			{
				conditionMatched.Insert(entity);
			}
		}

		return conditionMatched;
	}

	//------------------------------------------------------------------------------------------------
	static bool Evaluate(notnull EDF_DbEntity entity, notnull EDF_DbFindCondition condition)
	{
		switch (condition.Type())
		{
			case EDF_DbFindAnd:
			{
				foreach (EDF_DbFindCondition checkCondition : EDF_DbFindAnd.Cast(condition).m_aConditions)
				{
					if (!Evaluate(entity, checkCondition))
						return false;
				}

				return true;
			}

			case EDF_DbFindOr:
			{
				foreach (EDF_DbFindCondition checkCondition : EDF_DbFindOr.Cast(condition).m_aConditions)
				{
					if (Evaluate(entity, checkCondition))
						return true;
				}

				return false;
			}

			default:
			{
				EDF_DbFindFieldCondition fieldCondition = EDF_DbFindFieldCondition.Cast(condition);

				if (!fieldCondition)
					return false;

				array<ref EDF_DbFindFieldPathSegment> segments = ParseSegments(fieldCondition);
				return EvaluateField(entity, fieldCondition, segments, 0);
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static array<ref EDF_DbFindFieldPathSegment> ParseSegments(EDF_DbFindFieldCondition fieldCondition)
	{
		array<ref EDF_DbFindFieldPathSegment> resultSegments();

		array<string> segments();
		fieldCondition.m_sFieldPath.Split(EDF_DbFindFieldAnnotations.SEPERATOR, segments, true);

		resultSegments.Reserve(segments.Count());

		foreach (string segment : segments)
		{
			int flags = 0; // Explicit reset to 0 is required

			while (true)
			{
				if (segment.Replace(EDF_DbFindFieldAnnotations.ANY, "") > 0)
				{
					flags |= EDF_DbFindFieldPathSegmentFlags.ANY;
					continue;
				}

				if (segment.Replace(EDF_DbFindFieldAnnotations.ALL, "") > 0)
				{
					flags |= EDF_DbFindFieldPathSegmentFlags.ALL;
					continue;
				}

				if (segment.Replace(EDF_DbFindFieldAnnotations.KEYS, "") > 0)
				{
					flags |= EDF_DbFindFieldPathSegmentFlags.KEYS;
					continue;
				}

				if (segment.Replace(EDF_DbFindFieldAnnotations.VALUES, "") > 0)
				{
					flags |= EDF_DbFindFieldPathSegmentFlags.VALUES;
					continue;
				}

				if (segment.Replace(EDF_DbFindFieldAnnotations.LENGTH, "") > 0)
				{
					flags |= EDF_DbFindFieldPathSegmentFlags.LENGTH;
					continue;
				}

				if (segment.Replace(EDF_DbFindFieldAnnotations.COUNT, "") > 0)
				{
					flags |= EDF_DbFindFieldPathSegmentFlags.COUNT;
					continue;
				}

				break;
			}

			int asIntValue = segment.ToInt();
			if (asIntValue != 0 || segment == "0")
			{
				flags |= EDF_DbFindFieldPathSegmentFlags.NUMBER;
			}
			else if (segment.ToType())
			{
				flags |= EDF_DbFindFieldPathSegmentFlags.TYPENAME;
			}

			resultSegments.Insert(new EDF_DbFindFieldPathSegment(segment, flags));
		}

		return resultSegments;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool EvaluateField(Class instance, EDF_DbFindFieldCondition fieldCondition, array<ref EDF_DbFindFieldPathSegment> pathSegments, int currentSegmentIndex)
	{
		if (currentSegmentIndex >= pathSegments.Count())
		{
			Debug.Error(string.Format("Failed to evaluate invalid rule '%1' on '%2'.", fieldCondition.m_sFieldPath, instance));
			return false;
		}

		ScriptModule scriptModule = GetGame().GetScriptModule();

		EDF_DbFindFieldPathSegment currentSegment = pathSegments.Get(currentSegmentIndex);
		EDF_ReflectionVariableInfo variableInfo = EDF_ReflectionVariableInfo.Get(instance, currentSegment.m_sFieldName);

		// Expand complex/collection type as this is not yet the final path segment
		if (currentSegmentIndex < pathSegments.Count() - 1)
		{
			if (variableInfo.m_tVaribleType.IsInherited(Class))
			{
				Class complexFieldValue;
				if (!instance.Type().GetVariableValue(instance, variableInfo.m_iVariableindex, complexFieldValue))
				{
					Debug.Error(string.Format("Failed to read field '%1' of type '%2' on '%3'.", currentSegment.m_sFieldName, variableInfo.m_tVaribleType, instance));
					return false;
				}

				// Expand collections
				if (variableInfo.m_eCollectionType != EDF_ReflectionVariableType.NONE)
				{
					int collectionCount;
					scriptModule.Call(complexFieldValue, "Count", false, collectionCount);

					int nextSegmentIndex = currentSegmentIndex + 1;
					EDF_DbFindFieldPathSegment nextSegment = pathSegments.Get(nextSegmentIndex);

					// Handle collection<Class>.typename access operator
					typename filterType = typename.Empty;
					if (nextSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.TYPENAME)
					{
						filterType = EDF_DbName.GetTypeByName(nextSegment.m_sFieldName);
						nextSegmentIndex++; //Skip source.filterType. to condition after the filter
						currentSegment = nextSegment; // move current segment because :any/:all modifier is on the typename segement
					}

					// Handle collection<...>.N access operator
					int collectionStartIndex = 0;
					bool indexAccessorMode = false;
					if (nextSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.NUMBER)
					{
						collectionStartIndex = nextSegment.m_sFieldName.ToInt();

						if (collectionStartIndex >= collectionCount)
						{
							Debug.Error(string.Format("Tried to access ilegal collection index '%1' of type '%2' on '%3'. Collection only contained '%4' items.", collectionStartIndex, variableInfo.m_tVaribleType, instance, collectionCount));
							return false;
						}

						indexAccessorMode = true;
						nextSegmentIndex++; // Continue on the segment after the index information
					}

					for (int nValue = collectionStartIndex; nValue < collectionCount; nValue++)
					{
						Class collectionValueItem;

						string getFnc = "Get";
						if (variableInfo.m_eCollectionType == EDF_ReflectionVariableType.MAP)
						{
							// Access n-th map element by key(default) or value
							if (currentSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.VALUES) //Only if Values() was explicitly set
							{
								getFnc = "GetElement";
							}
							else
							{
								getFnc = "GetKey";
							}
						}

						if (!scriptModule.Call(complexFieldValue, getFnc, false, collectionValueItem, nValue) || !collectionValueItem)
						{
							Debug.Error(string.Format("Failed to get collection value at index '%1' on collection '%2' on '%3'.", nValue, complexFieldValue, instance));
							return false;
						}

						if (filterType && !collectionValueItem.IsInherited(filterType))
							continue;

						bool evaluationResult = EvaluateField(collectionValueItem, fieldCondition, pathSegments, nextSegmentIndex);

						// A specific index of the collection was checked, just directly return that result what ever it is.
						if (indexAccessorMode)
							return evaluationResult;

						if ((currentSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.ALL))
						{
							// All must be true, but at least one was false, so total result is false
							if (!evaluationResult)
								return false;

							// All elements were iterated and none of them returned false, so the overall result is true
							if (nValue == collectionCount - 1)
								return true;
						}

						//Any(default) must be true, and we got one match, so we can abort and return true
						if (evaluationResult)
							return true;
					}

					//None of the typename filters matched or none of the any-conditions returned true
					return false;
				}

				// Expand complex type
				return EvaluateField(complexFieldValue, fieldCondition, pathSegments, currentSegmentIndex + 1);
			}
			else
			{
				Debug.Error(string.Format("Reading field '%1' of a primtive type '%2' on '%3' is not possible.", currentSegment.m_sFieldName, variableInfo.m_tVaribleType, instance));
				return false;
			}
		}

		// Apply condition to result field value
		switch (fieldCondition.Type())
		{
			case EDF_DbFindCheckFieldNullOrDefault:
			{
				switch (variableInfo.m_tVaribleType)
				{
					case int: return EDF_DbFindFieldFieldNullOrDefaultChecker<int>.Evaluate(instance, EDF_DbFindCheckFieldNullOrDefault.Cast(fieldCondition), currentSegment, variableInfo);
					case float: return EDF_DbFindFieldFieldNullOrDefaultChecker<float>.Evaluate(instance, EDF_DbFindCheckFieldNullOrDefault.Cast(fieldCondition), currentSegment, variableInfo);
					case bool: return EDF_DbFindFieldFieldNullOrDefaultChecker<bool>.Evaluate(instance, EDF_DbFindCheckFieldNullOrDefault.Cast(fieldCondition), currentSegment, variableInfo);
					case string: return EDF_DbFindFieldFieldNullOrDefaultChecker<string>.Evaluate(instance, EDF_DbFindCheckFieldNullOrDefault.Cast(fieldCondition), currentSegment, variableInfo);
					case vector: return EDF_DbFindFieldFieldNullOrDefaultChecker<vector>.Evaluate(instance, EDF_DbFindCheckFieldNullOrDefault.Cast(fieldCondition), currentSegment, variableInfo);
				}

				return EDF_DbFindFieldFieldNullOrDefaultChecker<Class>.Evaluate(instance, EDF_DbFindCheckFieldNullOrDefault.Cast(fieldCondition), currentSegment, variableInfo);
			}

			case EDF_DbFindFieldInt:
			{
				EDF_DbFindFieldInt typedCondition = EDF_DbFindFieldInt.Cast(fieldCondition);
				return EDF_DbFindFieldValueTypedEvaluator<int>.Evaluate(
					instance,
					typedCondition.m_aComparisonValues,
					typedCondition.m_eComparisonOperator,
					typedCondition.m_bStringsInvariant,
					typedCondition.m_bStringsPartialMatches,
					currentSegment,
					variableInfo);
			}

			case EDF_DbFindFieldIntArray:
			{
				EDF_DbFindFieldIntArray typedCondition = EDF_DbFindFieldIntArray.Cast(fieldCondition);
				foreach (array<int> compareValues : typedCondition.m_aComparisonValues)
				{
					if (EDF_DbFindFieldValueTypedEvaluator<int>.Evaluate(
						instance,
						compareValues,
						typedCondition.m_eComparisonOperator,
						typedCondition.m_bStringsInvariant,
						typedCondition.m_bStringsPartialMatches,
						currentSegment,
						variableInfo,
						true))
					{
						return true;
					}
				}

				return false;
			}

			case EDF_DbFindFieldFloat:
			{
				EDF_DbFindFieldFloat typedCondition = EDF_DbFindFieldFloat.Cast(fieldCondition);
				return EDF_DbFindFieldValueTypedEvaluator<float>.Evaluate(
					instance,
					typedCondition.m_aComparisonValues,
					typedCondition.m_eComparisonOperator,
					typedCondition.m_bStringsInvariant,
					typedCondition.m_bStringsPartialMatches,
					currentSegment,
					variableInfo);
			}

			case EDF_DbFindFieldFloatArray:
			{
				EDF_DbFindFieldFloatArray typedCondition = EDF_DbFindFieldFloatArray.Cast(fieldCondition);
				foreach (array<float> compareValues : typedCondition.m_aComparisonValues)
				{
					if (EDF_DbFindFieldValueTypedEvaluator<float>.Evaluate(
						instance,
						compareValues,
						typedCondition.m_eComparisonOperator,
						typedCondition.m_bStringsInvariant,
						typedCondition.m_bStringsPartialMatches,
						currentSegment,
						variableInfo,
						true))
					{
						return true;
					}
				}

				return false;
			}

			case EDF_DbFindFieldBool:
			{
				EDF_DbFindFieldBool typedCondition = EDF_DbFindFieldBool.Cast(fieldCondition);
				return EDF_DbFindFieldValueTypedEvaluator<bool>.Evaluate(
					instance,
					typedCondition.m_aComparisonValues,
					typedCondition.m_eComparisonOperator,
					typedCondition.m_bStringsInvariant,
					typedCondition.m_bStringsPartialMatches,
					currentSegment,
					variableInfo);
			}

			case EDF_DbFindFieldBoolArray:
			{
				EDF_DbFindFieldBoolArray typedCondition = EDF_DbFindFieldBoolArray.Cast(fieldCondition);
				foreach (array<bool> compareValues : typedCondition.m_aComparisonValues)
				{
					if (EDF_DbFindFieldValueTypedEvaluator<bool>.Evaluate(
						instance,
						compareValues,
						typedCondition.m_eComparisonOperator,
						typedCondition.m_bStringsInvariant,
						typedCondition.m_bStringsPartialMatches,
						currentSegment,
						variableInfo,
						true))
					{
						return true;
					}
				}

				return false;
			}

			case EDF_DbFindFieldString:
			{
				EDF_DbFindFieldString typedCondition = EDF_DbFindFieldString.Cast(fieldCondition);
				return EDF_DbFindFieldValueTypedEvaluator<string>.Evaluate(
					instance,
					typedCondition.m_aComparisonValues,
					typedCondition.m_eComparisonOperator,
					typedCondition.m_bStringsInvariant,
					typedCondition.m_bStringsPartialMatches,
					currentSegment,
					variableInfo);
			}

			case EDF_DbFindFieldStringArray:
			{
				EDF_DbFindFieldStringArray typedCondition = EDF_DbFindFieldStringArray.Cast(fieldCondition);
				foreach (array<string> compareValues : typedCondition.m_aComparisonValues)
				{
					if (EDF_DbFindFieldValueTypedEvaluator<string>.Evaluate(
						instance,
						compareValues,
						typedCondition.m_eComparisonOperator,
						typedCondition.m_bStringsInvariant,
						typedCondition.m_bStringsPartialMatches,
						currentSegment,
						variableInfo,
						true))
					{
						return true;
					}
				}

				return false;
			}

			case EDF_DbFindFieldVector:
			{
				EDF_DbFindFieldVector typedCondition = EDF_DbFindFieldVector.Cast(fieldCondition);
				return EDF_DbFindFieldValueTypedEvaluator<vector>.Evaluate(
					instance,
					typedCondition.m_aComparisonValues,
					typedCondition.m_eComparisonOperator,
					typedCondition.m_bStringsInvariant,
					typedCondition.m_bStringsPartialMatches,
					currentSegment,
					variableInfo);
			}

			case EDF_DbFindFieldVectorArray:
			{
				EDF_DbFindFieldVectorArray typedCondition = EDF_DbFindFieldVectorArray.Cast(fieldCondition);
				foreach (array<vector> compareValues : typedCondition.m_aComparisonValues)
				{
					if (EDF_DbFindFieldValueTypedEvaluator<vector>.Evaluate(
						instance,
						compareValues,
						typedCondition.m_eComparisonOperator,
						typedCondition.m_bStringsInvariant,
						typedCondition.m_bStringsPartialMatches,
						currentSegment,
						variableInfo,
						true))
					{
						return true;
					}
				}

				return false;
			}

			case EDF_DbFindFieldTypename:
			{
				EDF_DbFindFieldTypename typedCondition = EDF_DbFindFieldTypename.Cast(fieldCondition);
				return EDF_DbFindFieldValueTypedEvaluator<typename>.Evaluate(
					instance,
					typedCondition.m_aComparisonValues,
					typedCondition.m_eComparisonOperator,
					typedCondition.m_bStringsInvariant,
					typedCondition.m_bStringsPartialMatches,
					currentSegment,
					variableInfo);
			}

			case EDF_DbFindFieldTypenameArray:
			{
				EDF_DbFindFieldTypenameArray typedCondition = EDF_DbFindFieldTypenameArray.Cast(fieldCondition);
				foreach (array<typename> compareValues : typedCondition.m_aComparisonValues)
				{
					if (EDF_DbFindFieldValueTypedEvaluator<typename>.Evaluate(
						instance,
						compareValues,
						typedCondition.m_eComparisonOperator,
						typedCondition.m_bStringsInvariant,
						typedCondition.m_bStringsPartialMatches,
						currentSegment,
						variableInfo,
						true))
					{
						return true;
					}
				}

				return false;
			}

			default:
			{
				Debug.Error(string.Format("Unknown condition type '%1'.", fieldCondition.Type().ToString()));
				return false;
			}
		}

		// Fall through
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Collects id field comparision values and returns true if there are no other fields that conditions need to be applied to
	static bool CollectConditionIds(EDF_DbFindCondition condition, out set<string> findIds, out set<string> skipIds)
	{
		EDF_DbFindFieldString stringMultipleCondition = EDF_DbFindFieldString.Cast(condition);
		if (stringMultipleCondition)
		{
			if (stringMultipleCondition.m_sFieldPath != EDF_DbEntity.FIELD_ID ||
				(stringMultipleCondition.m_eComparisonOperator != EDF_EDbFindOperator.EQUAL &&
				stringMultipleCondition.m_eComparisonOperator != EDF_EDbFindOperator.NOT_EQUAL))
			{
				return false;
			}

			foreach (string id : stringMultipleCondition.m_aComparisonValues)
			{
				if (stringMultipleCondition.m_eComparisonOperator == EDF_EDbFindOperator.EQUAL)
				{
					findIds.Insert(id);
				}
				else
				{
					skipIds.Insert(id);
				}
			}

			return true;
		}

		EDF_DbFindConditionWithChildren conditionWithChildren = EDF_DbFindConditionWithChildren.Cast(condition);
		if (conditionWithChildren)
		{
			bool isComplex = false;
			foreach (EDF_DbFindCondition childCondition : conditionWithChildren.m_aConditions)
			{
				if (!CollectConditionIds(childCondition, findIds, skipIds))
					isComplex = true;
			}

			return !isComplex;
		}

		// TODO: Can be optimized to check if id field is part of AND, and there are no other toplevel ORs, so we can know that only specific ids need to be loaded and then filters applied.
		return false;
	}
};

class EDF_DbFindFieldFieldNullOrDefaultChecker<Class TValueType>
{
	//------------------------------------------------------------------------------------------------
	static bool Evaluate(Class instance, EDF_DbFindCheckFieldNullOrDefault valueCondition, EDF_DbFindFieldPathSegment currentSegment, EDF_ReflectionVariableInfo fieldInfo)
	{
		TValueType fieldValue;
		if (!instance.Type().GetVariableValue(instance, fieldInfo.m_iVariableindex, fieldValue))
			return false;

		return IsNullOrDefault(fieldValue) == valueCondition.m_ShouldBeNullOrDefault;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsNullOrDefault(int value)
	{
		return value == 0;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsNullOrDefault(float value)
	{
		return float.AlmostEqual(value, 0.0);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsNullOrDefault(bool value)
	{
		return value == false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsNullOrDefault(string value)
	{
		value.Replace(" ", "");
		return value.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsNullOrDefault(vector value)
	{
		return float.AlmostEqual(vector.Distance(value, "0 0 0"), 0.0);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsNullOrDefault(Class value)
	{
		if (!value)
			return true;

		if (value.Type().IsInherited(array) || value.Type().IsInherited(set) || value.Type().IsInherited(map))
		{
			int collectionCount;
			if (!GetGame().GetScriptModule().Call(value, "Count", false, collectionCount))
				return false;

			return collectionCount == 0;
		}

		return false;
	}
};

class EDF_DbFindFieldValueTypedEvaluator<Class TValueType>
{
	//------------------------------------------------------------------------------------------------
	static bool Evaluate(
		Class instance,
		array<TValueType> comparisonValues,
		EDF_EDbFindOperator comparisonOperator,
		bool invariant,
		bool partialMatch,
		EDF_DbFindFieldPathSegment currentSegment,
		EDF_ReflectionVariableInfo fieldInfo,
		bool strictArrayEquality = false)
	{
		if (comparisonValues.IsEmpty())
		{
			Debug.Error(string.Format("Can not compare field '%1' on '%2' with empty condition.", currentSegment.m_sFieldName, instance));
			return false;
		}

		// We need an explicit var here or else the array gets lost when just re-assigned into function param directly.
		array<TValueType> invariantValues;
		if (invariant)
		{
			invariantValues = MakeInvariant(comparisonValues);
			comparisonValues = invariantValues;
		}

		ScriptModule scriptModule = GetGame().GetScriptModule();

		// Handle collection comparison
		int matchIdx = -1;
		if (fieldInfo.m_eCollectionType != EDF_ReflectionVariableType.NONE)
		{
			bool containsAll = comparisonOperator == EDF_EDbFindOperator.CONTAINS_ALL;
			bool containsAllOperation = containsAll || comparisonOperator== EDF_EDbFindOperator.NOT_CONTAINS_ALL;

			Class collectionHolder;
			if (!instance.Type().GetVariableValue(instance, fieldInfo.m_iVariableindex, collectionHolder))
				return false;

			int collectionCount;
			if (!scriptModule.Call(collectionHolder, "Count", false, collectionCount))
				return false;

			// Handle count of collection comparison
			if (currentSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.COUNT)
				return CompareCollectionCount(collectionCount, comparisonOperator, comparisonValues);

			int comparisonCount = comparisonValues.Count();
			if ((strictArrayEquality || containsAllOperation) && (collectionCount != comparisonCount))
			{
				if (comparisonOperator == EDF_EDbFindOperator.EQUAL)
					return false;

				if (comparisonOperator == EDF_EDbFindOperator.NOT_EQUAL)
					return true;

				if (collectionCount < comparisonCount)
					return comparisonOperator == EDF_EDbFindOperator.NOT_CONTAINS_ALL;
			}

			if (TValueType == string && !partialMatch)
			{
				if (comparisonOperator == EDF_EDbFindOperator.CONTAINS || comparisonOperator == EDF_EDbFindOperator.CONTAINS_ALL)
				{
					comparisonOperator = EDF_EDbFindOperator.EQUAL;
				}
				else
				{
					comparisonOperator = EDF_EDbFindOperator.NOT_EQUAL;
				}
			}

			for (int nItem = 0; nItem < collectionCount; nItem++)
			{
				TValueType fieldValue;

				string getFnc = "Get";

				if (fieldInfo.m_eCollectionType == EDF_ReflectionVariableType.MAP)
				{
					if (currentSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.VALUES) //Only if Values() was explicitly set
					{
						getFnc = "GetElement";
					}
					else
					{
						getFnc = "GetKey";
					}
				}

				if (TValueType == typename && fieldInfo.m_tCollectionValueType != typename)
				{
					// Special handling to read array<Class> and compare to array<typename>
					Class holder;
					if (!scriptModule.Call(collectionHolder, getFnc, false, holder, nItem) ||
						!scriptModule.Call(holder, "Type", false, fieldValue, holder))
						return false;
				}
				else if (!scriptModule.Call(collectionHolder, getFnc, false, fieldValue, nItem))
				{
					return false;
				}

				array<TValueType> compareValues = comparisonValues;
				if (strictArrayEquality)
					compareValues = {comparisonValues.Get(nItem)};

				matchIdx = -1;
				bool comparisonMatches = Compare(fieldValue, comparisonOperator, compareValues, matchIdx, invariant, partialMatch);

				if (containsAllOperation)
				{
					if (comparisonMatches)
					{
						// All were contained
						if (comparisonValues.Count() == 1)
							return containsAll;

						comparisonValues.RemoveOrdered(matchIdx);
					}

					continue;
				}

				if (strictArrayEquality || (currentSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.ALL))
				{
					// All must match the operator comparison, if one of them failes the final result is false
					if (!comparisonMatches)
						return false;

					// All including the last item in the collection matched the comparison, so we return true
					if (nItem == collectionCount - 1)
						return true;
				}
				else
				{
					// Any of the item matched, so we can return true early
					if (comparisonMatches)
						return true;

					// None, including the last item, matched the condition so fall through to false
				}
			}

			// Some contains all values left over
			if (containsAllOperation)
				return !containsAll;

			// Fall through
			return false;
		}

		// Special case of string lenth which is int value condition but string holder
		if (currentSegment.m_iFlags & EDF_DbFindFieldPathSegmentFlags.LENGTH)
		{
			string stringData;
			if (!instance.Type().GetVariableValue(instance, fieldInfo.m_iVariableindex, stringData))
				return false;

			Managed arr = comparisonValues;
			return Compare(StringLengthUtf8(stringData), comparisonOperator, array<int>.Cast(arr));
		}

		// Compare primitive value
		TValueType fieldValue;
		if (!instance.Type().GetVariableValue(instance, fieldInfo.m_iVariableindex, fieldValue))
			return false;

		return Compare(fieldValue, comparisonOperator, comparisonValues, matchIdx, invariant, partialMatch);
	}

	//------------------------------------------------------------------------------------------------
	// Default do nothing implementation
	protected static array<TValueType> MakeInvariant(Class comparisonValues);

	//------------------------------------------------------------------------------------------------
	protected static array<string> MakeInvariant(array<string> comparisonValues)
	{
		array<string> copy();
		copy.Reserve(comparisonValues.Count());
		foreach (string comparisonValue : comparisonValues)
		{
			comparisonValue.ToLower();
			copy.Insert(comparisonValue);
		}
		return copy;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool CompareCollectionCount(int collectionCount, EDF_EDbFindOperator operator, Managed comparisonValues)
	{
		array<int> strongTypedComparisonValues = array<int>.Cast(comparisonValues);
		if (!strongTypedComparisonValues)
			return false;

		return Compare(collectionCount, operator, strongTypedComparisonValues);
	}

	//------------------------------------------------------------------------------------------------
	//! TODO: Replace by native method once https://feedback.bistudio.com/T173104 is fixed
	protected static int StringLengthUtf8(string utf8string)
	{
		int length = 0;
		for (int nChar = 0, chars = utf8string.Length(); nChar < chars; nChar++)
		{
			int char = utf8string.ToAscii(nChar);
			if (!(char & 0x80) || (char & 0x40))
				length++;
		}

		return length;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Compare(int fieldValue, EDF_EDbFindOperator operator, array<int> comparisonValues, out int matchIdx = -1, bool invariant = false, bool partialMatch = false)
	{
		switch (operator)
		{
			case EDF_EDbFindOperator.EQUAL:
			case EDF_EDbFindOperator.CONTAINS:
			case EDF_EDbFindOperator.CONTAINS_ALL:
			{
				matchIdx = comparisonValues.Find(fieldValue);
				return matchIdx != -1;
			}

			case EDF_EDbFindOperator.NOT_EQUAL:
			case EDF_EDbFindOperator.NOT_CONTAINS:
			case EDF_EDbFindOperator.NOT_CONTAINS_ALL:
			{
				matchIdx = comparisonValues.Find(fieldValue);
				return matchIdx == -1;
			}

			case EDF_EDbFindOperator.LESS_THAN:
			{
				foreach (int idx, int compare : comparisonValues)
				{
					if (fieldValue < compare)
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.LESS_THAN_OR_EQUAL:
			{
				foreach (int idx, int compare : comparisonValues)
				{
					if (fieldValue <= compare)
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.GREATER_THAN:
			{
				foreach (int idx, int compare : comparisonValues)
				{
					if (fieldValue > compare)
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.GREATER_THAN_OR_EQUAL:
			{
				foreach (int idx, int compare : comparisonValues)
				{
					if (fieldValue >= compare)
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Compare(float fieldValue, EDF_EDbFindOperator operator, array<float> comparisonValues, out int matchIdx = -1, bool invariant = false, bool partialMatch = false)
	{
		switch (operator)
		{
			case EDF_EDbFindOperator.EQUAL:
			case EDF_EDbFindOperator.CONTAINS:
			case EDF_EDbFindOperator.CONTAINS_ALL:
			{
				foreach (int idx, float compare : comparisonValues)
				{
					if (float.AlmostEqual(fieldValue, compare))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.NOT_EQUAL:
			case EDF_EDbFindOperator.NOT_CONTAINS:
			case EDF_EDbFindOperator.NOT_CONTAINS_ALL:
			{
				foreach (int idx, float compare : comparisonValues)
				{
					if (float.AlmostEqual(fieldValue, compare))
					{
						matchIdx = idx;
						return false;
					}
				}

				return true;
			}

			case EDF_EDbFindOperator.LESS_THAN:
			{
				foreach (int idx, float compare : comparisonValues)
				{
					if (fieldValue < compare)
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.LESS_THAN_OR_EQUAL:
			{
				foreach (int idx, float compare : comparisonValues)
				{
					if ((fieldValue < compare) || float.AlmostEqual(fieldValue, compare))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.GREATER_THAN:
			{
				foreach (int idx, float compare : comparisonValues)
				{
					if (fieldValue > compare)
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.GREATER_THAN_OR_EQUAL:
			{
				foreach (int idx, float compare : comparisonValues)
				{
					if ((fieldValue > compare) || float.AlmostEqual(fieldValue, compare))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Compare(bool fieldValue, EDF_EDbFindOperator operator, array<bool> comparisonValues, out int matchIdx = -1, bool invariant = false, bool partialMatch = false)
	{
		switch (operator)
		{
			case EDF_EDbFindOperator.EQUAL:
			case EDF_EDbFindOperator.CONTAINS:
			case EDF_EDbFindOperator.CONTAINS_ALL:
			{
				matchIdx = comparisonValues.Find(fieldValue);
				return matchIdx != -1;
			}

			case EDF_EDbFindOperator.NOT_EQUAL:
			case EDF_EDbFindOperator.NOT_CONTAINS:
			case EDF_EDbFindOperator.NOT_CONTAINS_ALL:
			{
				matchIdx = comparisonValues.Find(fieldValue);
				return matchIdx == -1;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Compare(string fieldValue, EDF_EDbFindOperator operator, array<string> comparisonValues, out int matchIdx = -1, bool invariant = false, bool partialMatch = false)
	{
		if (invariant)
			fieldValue.ToLower();

		switch (operator)
		{
			case EDF_EDbFindOperator.EQUAL:
			case EDF_EDbFindOperator.CONTAINS:
			case EDF_EDbFindOperator.CONTAINS_ALL:
			{
				if (operator == EDF_EDbFindOperator.EQUAL && !partialMatch)
				{
					matchIdx = comparisonValues.Find(fieldValue);
					return matchIdx != -1;
				}

				foreach (int idx, string compare : comparisonValues)
				{
					if (fieldValue.Contains(compare))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.NOT_EQUAL:
			case EDF_EDbFindOperator.NOT_CONTAINS:
			case EDF_EDbFindOperator.NOT_CONTAINS_ALL:
			{
				if (operator == EDF_EDbFindOperator.NOT_EQUAL && !partialMatch)
				{
					matchIdx = comparisonValues.Find(fieldValue);
					return matchIdx == -1;
				}

				foreach (int idx, string compare : comparisonValues)
				{
					if (fieldValue.Contains(compare))
					{
						matchIdx = idx;
						return false;
					}
				}

				return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Compare(vector fieldValue, EDF_EDbFindOperator operator, array<vector> comparisonValues, out int matchIdx = -1, bool invariant = false, bool partialMatch = false)
	{
		switch (operator)
		{
			case EDF_EDbFindOperator.EQUAL:
			case EDF_EDbFindOperator.CONTAINS:
			case EDF_EDbFindOperator.CONTAINS_ALL:
			{
				foreach (int idx, vector compare : comparisonValues)
				{
					if (float.AlmostEqual(vector.Distance(fieldValue, compare), 0))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.NOT_EQUAL:
			case EDF_EDbFindOperator.NOT_CONTAINS:
			case EDF_EDbFindOperator.NOT_CONTAINS_ALL:
			{
				foreach (int idx, vector compare : comparisonValues)
				{
					if (float.AlmostEqual(vector.Distance(fieldValue, compare), 0))
					{
						matchIdx = idx;
						return false;
					}
				}

				return true;
			}

			case EDF_EDbFindOperator.LESS_THAN:
			{
				foreach (int idx, vector compare : comparisonValues)
				{
					if ((fieldValue[0] < compare[0]) &&
						(fieldValue[1] < compare[1]) &&
						(fieldValue[2] < compare[2]))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.LESS_THAN_OR_EQUAL:
			{
				foreach (int idx, vector compare : comparisonValues)
				{
					if (((fieldValue[0] < compare[0]) || float.AlmostEqual(fieldValue[0], compare[0])) &&
						((fieldValue[1] < compare[1]) || float.AlmostEqual(fieldValue[1], compare[1])) &&
						((fieldValue[2] < compare[2]) || float.AlmostEqual(fieldValue[2], compare[2])))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.GREATER_THAN:
			{
				foreach (int idx, vector compare : comparisonValues)
				{
					if ((fieldValue[0] > compare[0]) &&
						(fieldValue[1] > compare[1]) &&
						(fieldValue[2] > compare[2]))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.GREATER_THAN_OR_EQUAL:
			{
				foreach (int idx, vector compare : comparisonValues)
				{
					if (((fieldValue[0] > compare[0]) || float.AlmostEqual(fieldValue[0], compare[0])) &&
						((fieldValue[1] > compare[1]) || float.AlmostEqual(fieldValue[1], compare[1])) &&
						((fieldValue[2] > compare[2]) || float.AlmostEqual(fieldValue[2], compare[2])))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Compare(typename fieldValue, EDF_EDbFindOperator operator, array<typename> comparisonValues, out int matchIdx = -1, bool invariant = false, bool partialMatch = false)
	{
		switch (operator)
		{
			case EDF_EDbFindOperator.EQUAL:
			case EDF_EDbFindOperator.CONTAINS:
			case EDF_EDbFindOperator.CONTAINS_ALL:
			{
				foreach (int idx, typename compare : comparisonValues)
				{
					if (fieldValue.IsInherited(compare))
					{
						matchIdx = idx;
						return true;
					}
				}

				return false;
			}

			case EDF_EDbFindOperator.NOT_EQUAL:
			case EDF_EDbFindOperator.NOT_CONTAINS:
			case EDF_EDbFindOperator.NOT_CONTAINS_ALL:
			{
				foreach (int idx, typename compare : comparisonValues)
				{
					if (fieldValue.IsInherited(compare))
					{
						matchIdx = idx;
						return false;
					}
				}

				return true;
			}
		}

		return false;
	}
};
