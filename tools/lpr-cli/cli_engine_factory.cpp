    const auto ocr_input_name = required(contract, "ocr.input_name");
    const auto& ocr_input_descriptor = descriptor_named(
        ocr_session->inputs(), ocr_input_name, "OCR input");
    infrastructure::lprnet::LprNetPreprocessSemantics semantics{};
    semantics.color_order = color_order(required(contract, "ocr.color_order"));
    semantics.input_scale = parse_float(contract, "ocr.input_scale");
    semantics.mean = parse_float3(contract, "ocr.mean");
    semantics.standard_deviation = parse_float3(contract, "ocr.std");
    const auto ocr_input_spec = infrastructure::lprnet::make_lprnet_input_spec(
        ocr_input_descriptor, semantics);

    infrastructure::lprnet::LprNetOnnxOcrAdapterConfig ocr_config{};
    ocr_config.provider_name = "lprnet_onnx";
    ocr_config.model_version = required(contract, "ocr.model_version");
    ocr_config.input_name = ocr_input_name;
    ocr_config.output_name = required(contract, "ocr.output_name");
    ocr_config.input = ocr_input_spec;
    ocr_config.output_layout = output_layout(required(contract, "ocr.output_layout"));
    const auto charset = required(contract, "ocr.charset");
    ocr_config.decoder.ctc.charset.assign(charset.begin(), charset.end());
    ocr_config.decoder.ctc.blank_index = parse_size(contract, "ocr.blank_index");
    ocr_config.decoder.ctc.maximum_timesteps = parse_size(contract, "ocr.maximum_timesteps");
    ocr_config.decoder.ctc.maximum_classes = parse_size(contract, "ocr.maximum_classes");
    ocr_config.decoder.beam_width = parse_size(contract, "ocr.beam_width");
    ocr_config.decoder.result_limit = parse_size(contract, "ocr.result_limit");
    ocr_config.decoder.classes_per_step = parse_size(contract, "ocr.classes_per_step");
    ocr_config.decoder.confusion_weight = parse_float(contract, "ocr.confusion_weight");

    const auto& ocr_output = descriptor_named(
        ocr_session->outputs(), ocr_config.output_name, "OCR output");
    validate_ocr_output_contract(